/* magma/assets/build_ui.c - MC 1.11.2 UI/entity atlas header generators (C11).
 * Outputs: gui_atlas.h, hand_atlas.h, hud_atlas.h, inventory_ui_atlas.h,
 * item_atlas.h, mob_atlas.h. Semantics match the audited Python builders. */
#include "build.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- growable text buffer ---- */

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} Buf;

static int buf_reserve(Buf *b, size_t need)
{
    size_t ncap;
    char *p;
    if (need <= b->cap)
        return 0;
    ncap = b->cap ? b->cap : 4096;
    while (ncap < need)
        ncap *= 2;
    p = (char *)realloc(b->data, ncap);
    if (!p)
        return -1;
    b->data = p;
    b->cap = ncap;
    return 0;
}

static int buf_append(Buf *b, const char *s, size_t n)
{
    if (buf_reserve(b, b->len + n + 1) != 0)
        return -1;
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
    return 0;
}

static int buf_puts(Buf *b, const char *s)
{
    return buf_append(b, s, strlen(s));
}

static int buf_printf(Buf *b, const char *fmt, ...)
{
    va_list ap;
    char stack[1024];
    char *heap = NULL;
    int n;
    int rc;

    va_start(ap, fmt);
    n = vsnprintf(stack, sizeof(stack), fmt, ap);
    va_end(ap);
    if (n < 0)
        return -1;
    if ((size_t)n < sizeof(stack))
        return buf_append(b, stack, (size_t)n);
    heap = (char *)malloc((size_t)n + 1);
    if (!heap)
        return -1;
    va_start(ap, fmt);
    vsnprintf(heap, (size_t)n + 1, fmt, ap);
    va_end(ap);
    rc = buf_append(b, heap, (size_t)n);
    free(heap);
    return rc;
}

static void buf_free(Buf *b)
{
    free(b->data);
    b->data = NULL;
    b->len = b->cap = 0;
}

/* ---- path helpers ---- */

static int path_join(char *out, size_t out_sz, const char *dir, const char *name)
{
    size_t dl, nl;
    int need_slash;
    if (!out || !dir || !name || out_sz == 0)
        return -1;
    dl = strlen(dir);
    nl = strlen(name);
    need_slash = (dl > 0 && dir[dl - 1] != '/');
    if (dl + (need_slash ? 1 : 0) + nl + 1 > out_sz)
        return -1;
    memcpy(out, dir, dl);
    if (need_slash)
        out[dl++] = '/';
    memcpy(out + dl, name, nl + 1);
    return 0;
}

static int write_header_atomic(const char *out_path, const Buf *b)
{
    char tmp[4096];
    FILE *f;
    size_t wr;
    size_t tlen;

    tlen = strlen(out_path);
    if (tlen + 5 >= sizeof(tmp))
        return -1;
    memcpy(tmp, out_path, tlen);
    memcpy(tmp + tlen, ".tmp", 5);

    f = fopen(tmp, "wb");
    if (!f)
        return -1;
    wr = fwrite(b->data ? b->data : "", 1, b->len, f);
    if (wr != b->len) {
        fclose(f);
        remove(tmp);
        return -1;
    }
    if (fflush(f) != 0) {
        fclose(f);
        remove(tmp);
        return -1;
    }
    if (fclose(f) != 0) {
        remove(tmp);
        return -1;
    }
    if (rename(tmp, out_path) != 0) {
        remove(tmp);
        return -1;
    }
    return 0;
}

/* Python: "    " + ",".join("%d" % b for b in chunk) + ",\n" */
static int emit_rgba_array(Buf *b, const unsigned char *px, size_t n, const char *indent)
{
    size_t i, j;
    for (i = 0; i < n; i += 16) {
        size_t chunk = n - i;
        if (chunk > 16)
            chunk = 16;
        if (buf_puts(b, indent) != 0)
            return -1;
        for (j = 0; j < chunk; j++) {
            char num[16];
            int len = snprintf(num, sizeof(num), "%d", (int)px[i + j]);
            if (len < 0)
                return -1;
            if (buf_append(b, num, (size_t)len) != 0)
                return -1;
            if (j + 1 < chunk) {
                if (buf_puts(b, ",") != 0)
                    return -1;
            }
        }
        if (buf_puts(b, ",\n") != 0)
            return -1;
    }
    return 0;
}

static int next_pow2(int n)
{
    int p = 1;
    if (n < 1)
        return 1;
    while (p < n)
        p <<= 1;
    return p;
}

static void upper_copy(char *dst, size_t dst_sz, const char *src)
{
    size_t i;
    for (i = 0; src[i] && i + 1 < dst_sz; i++)
        dst[i] = (char)toupper((unsigned char)src[i]);
    dst[i] = '\0';
}

/* ---- GUI ---- */

typedef struct {
    const char *sym;
    const char *src; /* under textures/ */
    int x, y, w, h;
} GuiCrop;

static const GuiCrop GUI_SPRITES[] = {
    { "INV_PANEL", "gui/container/inventory.png", 0, 0, 176, 166 },
    { "TABLE_PANEL", "gui/container/crafting_table.png", 0, 0, 176, 166 },
    { "FURNACE_PANEL", "gui/container/furnace.png", 0, 0, 176, 166 },
    { "FURNACE_FLAME", "gui/container/furnace.png", 176, 0, 14, 14 },
    { "FURNACE_ARROW", "gui/container/furnace.png", 176, 14, 24, 17 },
    { "FONT", "font/ascii.png", 0, 0, 128, 128 },
};

typedef struct {
    int item_id;
    const char *src; /* under textures/ */
} GuiItemIcon;

static const GuiItemIcon GUI_ITEM_ICONS[] = {
    { 1, "blocks/stone.png" },
    { 3, "blocks/dirt.png" },
    { 4, "blocks/cobblestone.png" },
    { 5, "blocks/planks_oak.png" },
    { 12, "blocks/sand.png" },
    { 13, "blocks/gravel.png" },
    { 15, "blocks/iron_ore.png" },
    { 16, "blocks/coal_ore.png" },
    { 17, "blocks/log_oak.png" },
    { 20, "blocks/glass.png" },
    { 35, "blocks/wool_colored_white.png" },
    { 49, "blocks/obsidian.png" },
    { 50, "blocks/torch_on.png" },
    { 58, "blocks/crafting_table_front.png" },
    { 61, "blocks/furnace_front_off.png" },
    { 256, "items/iron_shovel.png" },
    { 257, "items/iron_pickaxe.png" },
    { 258, "items/iron_axe.png" },
    { 259, "items/flint_and_steel.png" },
    { 260, "items/apple.png" },
    { 261, "items/bow_standby.png" },
    { 262, "items/arrow.png" },
    { 263, "items/coal.png" },
    { 264, "items/diamond.png" },
    { 265, "items/iron_ingot.png" },
    { 266, "items/gold_ingot.png" },
    { 267, "items/iron_sword.png" },
    { 268, "items/wood_sword.png" },
    { 269, "items/wood_shovel.png" },
    { 270, "items/wood_pickaxe.png" },
    { 271, "items/wood_axe.png" },
    { 272, "items/stone_sword.png" },
    { 273, "items/stone_shovel.png" },
    { 274, "items/stone_pickaxe.png" },
    { 275, "items/stone_axe.png" },
    { 276, "items/diamond_sword.png" },
    { 277, "items/diamond_shovel.png" },
    { 278, "items/diamond_pickaxe.png" },
    { 279, "items/diamond_axe.png" },
    { 280, "items/stick.png" },
    { 287, "items/string.png" },
    { 288, "items/feather.png" },
    { 289, "items/gunpowder.png" },
    { 295, "items/seeds_wheat.png" },
    { 296, "items/wheat.png" },
    { 297, "items/bread.png" },
    { 318, "items/flint.png" },
    { 319, "items/porkchop_raw.png" },
    { 320, "items/porkchop_cooked.png" },
    { 325, "items/bucket_empty.png" },
    { 326, "items/bucket_water.png" },
    { 327, "items/bucket_lava.png" },
    { 334, "items/leather.png" },
    { 344, "items/egg.png" },
    { 352, "items/bone.png" },
    { 355, "items/bed.png" },
    { 363, "items/beef_raw.png" },
    { 364, "items/beef_cooked.png" },
    { 365, "items/chicken_raw.png" },
    { 366, "items/chicken_cooked.png" },
    { 367, "items/rotten_flesh.png" },
    { 368, "items/ender_pearl.png" },
    { 369, "items/blaze_rod.png" },
    { 377, "items/blaze_powder.png" },
    { 381, "items/ender_eye.png" },
};

typedef struct {
    char name[48];
    int w, h, off;
} SpriteRec;

static int blob_append(unsigned char **blob, size_t *blen, size_t *bcap,
                       const unsigned char *src, size_t n)
{
    if (*blen + n > *bcap) {
        size_t ncap = *bcap ? *bcap : 65536;
        unsigned char *p;
        while (ncap < *blen + n)
            ncap *= 2;
        p = (unsigned char *)realloc(*blob, ncap);
        if (!p)
            return -1;
        *blob = p;
        *bcap = ncap;
    }
    memcpy(*blob + *blen, src, n);
    *blen += n;
    return 0;
}

static int build_gui(AssetJar *jar, const char *out_path)
{
    static const char TEX[] = "assets/minecraft/textures/";
    unsigned char *blob = NULL;
    size_t blen = 0, bcap = 0;
    SpriteRec *table = NULL;
    size_t tcount = 0, tcap = 0;
    int icons[128][2];
    int n_icons = 0;
    size_t i;
    Buf b = { 0 };
    int rc = -1;
    char member[256];
    int n_named;

    for (i = 0; i < sizeof(GUI_SPRITES) / sizeof(GUI_SPRITES[0]); i++) {
        AssetImage full = { 0 }, crop = { 0 };
        SpriteRec *rec;
        snprintf(member, sizeof(member), "%s%s", TEX, GUI_SPRITES[i].src);
        if (asset_image_load(jar, member, &full) != 0)
            goto done;
        if (asset_image_crop(&full, GUI_SPRITES[i].x, GUI_SPRITES[i].y,
                             GUI_SPRITES[i].w, GUI_SPRITES[i].h, &crop) != 0) {
            asset_image_free(&full);
            goto done;
        }
        asset_image_free(&full);
        if (tcount + 1 > tcap) {
            size_t nc = tcap ? tcap * 2 : 16;
            SpriteRec *p = (SpriteRec *)realloc(table, nc * sizeof(*table));
            if (!p) {
                asset_image_free(&crop);
                goto done;
            }
            table = p;
            tcap = nc;
        }
        rec = &table[tcount];
        snprintf(rec->name, sizeof(rec->name), "%s", GUI_SPRITES[i].sym);
        rec->w = crop.w;
        rec->h = crop.h;
        rec->off = (int)blen;
        if (blob_append(&blob, &blen, &bcap, crop.rgba, (size_t)crop.w * crop.h * 4) != 0) {
            asset_image_free(&crop);
            goto done;
        }
        asset_image_free(&crop);
        tcount++;
    }

    /* CHEST_PANEL composite */
    {
        AssetImage g54 = { 0 }, top = { 0 }, bot = { 0 }, chest = { 0 };
        SpriteRec *rec;
        int top_h = 3 * 18 + 17;
        snprintf(member, sizeof(member), "%sgui/container/generic_54.png", TEX);
        if (asset_image_load(jar, member, &g54) != 0)
            goto done;
        if (asset_image_new(&chest, 176, 167) != 0) {
            asset_image_free(&g54);
            goto done;
        }
        memset(chest.rgba, 0, (size_t)176 * 167 * 4);
        if (asset_image_crop(&g54, 0, 0, 176, top_h, &top) != 0 ||
            asset_image_crop(&g54, 0, 126, 176, 96, &bot) != 0 ||
            asset_image_paste(&chest, &top, 0, 0) != 0 ||
            asset_image_paste(&chest, &bot, 0, top_h) != 0) {
            asset_image_free(&g54);
            asset_image_free(&top);
            asset_image_free(&bot);
            asset_image_free(&chest);
            goto done;
        }
        asset_image_free(&g54);
        asset_image_free(&top);
        asset_image_free(&bot);
        if (tcount + 1 > tcap) {
            size_t nc = tcap * 2;
            SpriteRec *p = (SpriteRec *)realloc(table, nc * sizeof(*table));
            if (!p) {
                asset_image_free(&chest);
                goto done;
            }
            table = p;
            tcap = nc;
        }
        rec = &table[tcount];
        snprintf(rec->name, sizeof(rec->name), "CHEST_PANEL");
        rec->w = 176;
        rec->h = 167;
        rec->off = (int)blen;
        if (blob_append(&blob, &blen, &bcap, chest.rgba, (size_t)176 * 167 * 4) != 0) {
            asset_image_free(&chest);
            goto done;
        }
        asset_image_free(&chest);
        tcount++;
    }

    /* EFFECT_PANEL, EFFECT_ICONS from inventory.png */
    {
        AssetImage inv = { 0 }, ep = { 0 }, ei = { 0 };
        SpriteRec *rec;
        snprintf(member, sizeof(member), "%sgui/container/inventory.png", TEX);
        if (asset_image_load(jar, member, &inv) != 0)
            goto done;
        if (asset_image_crop(&inv, 0, 166, 140, 32, &ep) != 0 ||
            asset_image_crop(&inv, 0, 198, 144, 54, &ei) != 0) {
            asset_image_free(&inv);
            asset_image_free(&ep);
            asset_image_free(&ei);
            goto done;
        }
        asset_image_free(&inv);
        for (i = 0; i < 2; i++) {
            AssetImage *im = (i == 0) ? &ep : &ei;
            const char *nm = (i == 0) ? "EFFECT_PANEL" : "EFFECT_ICONS";
            if (tcount + 1 > tcap) {
                size_t nc = tcap * 2;
                SpriteRec *p = (SpriteRec *)realloc(table, nc * sizeof(*table));
                if (!p) {
                    asset_image_free(&ep);
                    asset_image_free(&ei);
                    goto done;
                }
                table = p;
                tcap = nc;
            }
            rec = &table[tcount];
            snprintf(rec->name, sizeof(rec->name), "%s", nm);
            rec->w = im->w;
            rec->h = im->h;
            rec->off = (int)blen;
            if (blob_append(&blob, &blen, &bcap, im->rgba, (size_t)im->w * im->h * 4) != 0) {
                asset_image_free(&ep);
                asset_image_free(&ei);
                goto done;
            }
            tcount++;
        }
        asset_image_free(&ep);
        asset_image_free(&ei);
    }

    n_named = (int)tcount;

    for (i = 0; i < sizeof(GUI_ITEM_ICONS) / sizeof(GUI_ITEM_ICONS[0]); i++) {
        AssetImage img = { 0 };
        SpriteRec *rec;
        snprintf(member, sizeof(member), "%s%s", TEX, GUI_ITEM_ICONS[i].src);
        if (asset_image_load(jar, member, &img) != 0)
            goto done;
        if (img.w != 16 || img.h != 16) {
            asset_image_free(&img);
            goto done;
        }
        if (n_icons >= (int)(sizeof(icons) / sizeof(icons[0]))) {
            asset_image_free(&img);
            goto done;
        }
        icons[n_icons][0] = GUI_ITEM_ICONS[i].item_id;
        icons[n_icons][1] = (int)tcount;
        n_icons++;
        if (tcount + 1 > tcap) {
            size_t nc = tcap * 2;
            SpriteRec *p = (SpriteRec *)realloc(table, nc * sizeof(*table));
            if (!p) {
                asset_image_free(&img);
                goto done;
            }
            table = p;
            tcap = nc;
        }
        rec = &table[tcount];
        snprintf(rec->name, sizeof(rec->name), "ITEM_%d", GUI_ITEM_ICONS[i].item_id);
        rec->w = 16;
        rec->h = 16;
        rec->off = (int)blen;
        if (blob_append(&blob, &blen, &bcap, img.rgba, 16 * 16 * 4) != 0) {
            asset_image_free(&img);
            goto done;
        }
        asset_image_free(&img);
        tcount++;
    }

    /* icons already ascending by item id in table */
    {
        size_t expect = 0;
        for (i = 0; i < tcount; i++)
            expect += (size_t)table[i].w * (size_t)table[i].h * 4;
        if (expect != blen)
            goto done;
    }

    if (buf_puts(&b,
                 "/* GENERATED by make -C magma assets - DO NOT EDIT.\n"
                 " * Real MC 1.11.2 container GUI art: panels, furnace progress sprites,\n"
                 " * the ascii.png font sheet, and flat 16x16 item icons. Each sprite is\n"
                 " * an RGBA run (R,G,B,A per texel) in one flat array. */\n"
                 "#ifndef MAGMA_GUI_ATLAS_H\n#define MAGMA_GUI_ATLAS_H\n\n") != 0)
        goto done;
    if (buf_printf(&b, "#define GUI_SPRITE_COUNT %zu\n", tcount) != 0)
        goto done;
    if (buf_printf(&b, "#define GUI_ITEM_ICON_COUNT %d\n\n", n_icons) != 0)
        goto done;
    for (i = 0; i < (size_t)n_named; i++) {
        if (buf_printf(&b, "#define GUI_%s %zu\n", table[i].name, i) != 0)
            goto done;
    }
    if (buf_puts(&b,
                 "\ntypedef struct { const char *name; int w, h, off; } GuiSprite;\n"
                 "static const GuiSprite GUI_SPRITES[GUI_SPRITE_COUNT] = {\n") != 0)
        goto done;
    for (i = 0; i < tcount; i++) {
        if (buf_printf(&b, "    { \"%s\", %d, %d, %d },\n",
                       table[i].name, table[i].w, table[i].h, table[i].off) != 0)
            goto done;
    }
    if (buf_puts(&b,
                 "};\n\n"
                 "/* item id -> GUI_SPRITES index, ascending by item id */\n"
                 "typedef struct { int item, sprite; } GuiItemIcon;\n"
                 "static const GuiItemIcon GUI_ITEM_ICONS[GUI_ITEM_ICON_COUNT] = {\n") != 0)
        goto done;
    for (i = 0; i < (size_t)n_icons; i++) {
        if (buf_printf(&b, "    { %d, %d },\n", icons[i][0], icons[i][1]) != 0)
            goto done;
    }
    if (buf_printf(&b, "};\n\nstatic const unsigned char GUI_RGBA[%zu] = {\n", blen) != 0)
        goto done;
    if (emit_rgba_array(&b, blob, blen, "    ") != 0)
        goto done;
    if (buf_puts(&b, "};\n\n#endif /* MAGMA_GUI_ATLAS_H */\n") != 0)
        goto done;

    if (write_header_atomic(out_path, &b) != 0)
        goto done;
    rc = 0;
done:
    free(blob);
    free(table);
    buf_free(&b);
    return rc;
}

/* ---- HAND ---- */

static int build_hand(AssetJar *jar, const char *out_path)
{
    static const char *const SKINS[] = { "steve", "alex" };
    Buf b = { 0 };
    int rc = -1;
    size_t si;

    if (buf_puts(&b,
                 "/* GENERATED by make -C magma assets - DO NOT EDIT.\n"
                 " * Default MC 1.11.2 player skins (steve.png / alex.png), 64x64 RGBA.\n"
                 " * Byte order per texel: R,G,B,A (matches CrRgba memory layout).\n"
                 " * game/hand.c textures the first-person right-arm box from the\n"
                 " * arm sub-rect at texU=40,texV=16 (steve: 4x12x4 unwrap; alex\n"
                 " * \"slim\": 3x12x4 unwrap - see hand.c ARM_QUADS/ARM_QUADS_SLIM). */\n"
                 "#ifndef MAGMA_HAND_ATLAS_H\n#define MAGMA_HAND_ATLAS_H\n\n"
                 "#define HAND_SKIN_W 64\n"
                 "#define HAND_SKIN_H 64\n\n") != 0)
        goto done;

    for (si = 0; si < 2; si++) {
        char member[128];
        char uname[16];
        AssetImage img = { 0 };
        snprintf(member, sizeof(member),
                 "assets/minecraft/textures/entity/%s.png", SKINS[si]);
        if (asset_image_load(jar, member, &img) != 0)
            goto done;
        if (img.w != 64 || img.h != 64) {
            AssetImage canvas = { 0 };
            if (asset_image_new(&canvas, 64, 64) != 0) {
                asset_image_free(&img);
                goto done;
            }
            memset(canvas.rgba, 0, 64 * 64 * 4);
            if (asset_image_paste(&canvas, &img, 0, 0) != 0) {
                asset_image_free(&img);
                asset_image_free(&canvas);
                goto done;
            }
            asset_image_free(&img);
            img = canvas;
        }
        if (img.w != 64 || img.h != 64 || !img.rgba) {
            asset_image_free(&img);
            goto done;
        }
        upper_copy(uname, sizeof(uname), SKINS[si]);
        if (buf_printf(&b, "static const unsigned char HAND_SKIN_RGBA_%s[%d] = {\n",
                       uname, 64 * 64 * 4) != 0) {
            asset_image_free(&img);
            goto done;
        }
        if (emit_rgba_array(&b, img.rgba, 64 * 64 * 4, "    ") != 0) {
            asset_image_free(&img);
            goto done;
        }
        if (buf_puts(&b, "};\n\n") != 0) {
            asset_image_free(&img);
            goto done;
        }
        asset_image_free(&img);
    }

    if (buf_puts(&b,
                 "/* legacy alias: the non-slim default skin */\n"
                 "#define HAND_SKIN_RGBA HAND_SKIN_RGBA_STEVE\n\n"
                 "#endif /* MAGMA_HAND_ATLAS_H */\n") != 0)
        goto done;

    if (write_header_atomic(out_path, &b) != 0)
        goto done;
    rc = 0;
done:
    buf_free(&b);
    return rc;
}

/* ---- HUD ---- */

typedef struct {
    const char *sym;
    const char *src; /* relative to gui/ */
    int x, y, w, h;
} HudCrop;

static const HudCrop HUD_SPRITES[] = {
    { "HOTBAR", "widgets.png", 0, 0, 182, 22 },
    { "SELECT", "widgets.png", 0, 22, 24, 24 },
    { "CROSSHAIR", "icons.png", 0, 0, 15, 15 },
    { "HEART_BG", "icons.png", 16, 0, 9, 9 },
    { "HEART_BG_FLASH", "icons.png", 25, 0, 9, 9 },
    { "HEART_FULL", "icons.png", 52, 0, 9, 9 },
    { "HEART_HALF", "icons.png", 61, 0, 9, 9 },
    { "HEART_FLASH_FULL", "icons.png", 70, 0, 9, 9 },
    { "HEART_FLASH_HALF", "icons.png", 79, 0, 9, 9 },
    { "HEART_POISON_FULL", "icons.png", 88, 0, 9, 9 },
    { "HEART_POISON_HALF", "icons.png", 97, 0, 9, 9 },
    { "HEART_POISON_FLASH_FULL", "icons.png", 106, 0, 9, 9 },
    { "HEART_POISON_FLASH_HALF", "icons.png", 115, 0, 9, 9 },
    { "HEART_WITHER_FULL", "icons.png", 124, 0, 9, 9 },
    { "HEART_WITHER_HALF", "icons.png", 133, 0, 9, 9 },
    { "HEART_WITHER_FLASH_FULL", "icons.png", 142, 0, 9, 9 },
    { "HEART_WITHER_FLASH_HALF", "icons.png", 151, 0, 9, 9 },
    { "HEART_ABSORB_FULL", "icons.png", 160, 0, 9, 9 },
    { "HEART_ABSORB_HALF", "icons.png", 169, 0, 9, 9 },
    { "HUNGER_BG", "icons.png", 16, 27, 9, 9 },
    { "HUNGER_FULL", "icons.png", 52, 27, 9, 9 },
    { "HUNGER_HALF", "icons.png", 61, 27, 9, 9 },
    { "HUNGER_POISON_BG", "icons.png", 133, 27, 9, 9 },
    { "HUNGER_POISON_FULL", "icons.png", 88, 27, 9, 9 },
    { "HUNGER_POISON_HALF", "icons.png", 97, 27, 9, 9 },
    { "ARMOR_EMPTY", "icons.png", 16, 9, 9, 9 },
    { "ARMOR_HALF", "icons.png", 25, 9, 9, 9 },
    { "ARMOR_FULL", "icons.png", 34, 9, 9, 9 },
    { "AIR_FULL", "icons.png", 16, 18, 9, 9 },
    { "AIR_PARTIAL", "icons.png", 25, 18, 9, 9 },
    { "XP_EMPTY", "icons.png", 0, 64, 182, 5 },
    { "XP_FULL", "icons.png", 0, 69, 182, 5 },
    { "BOSS_PINK_BG", "bars.png", 0, 0, 182, 5 },
    { "BOSS_PINK_FULL", "bars.png", 0, 5, 182, 5 },
    { "POTION_BG", "container/inventory.png", 141, 166, 24, 24 },
    { "POTION_ICONS", "container/inventory.png", 0, 198, 144, 54 },
    { "BUTTON_DISABLED", "widgets.png", 0, 46, 200, 20 },
    { "BUTTON_ENABLED", "widgets.png", 0, 66, 200, 20 },
    { "BUTTON_HOVER", "widgets.png", 0, 86, 200, 20 },
};

static int build_hud(AssetJar *jar, const char *out_path)
{
    static const char GUI[] = "assets/minecraft/textures/gui/";
    static const char *const SRC_NAMES[] = {
        "widgets.png", "icons.png", "bars.png", "container/inventory.png"
    };
    AssetImage srcs[4] = { { 0 } };
    int src_ok[4] = { 0 };
    unsigned char *blob = NULL;
    size_t blen = 0, bcap = 0;
    SpriteRec table[64];
    size_t tcount = 0;
    size_t i, s;
    Buf b = { 0 };
    int rc = -1;
    char member[256];

    for (s = 0; s < 4; s++) {
        snprintf(member, sizeof(member), "%s%s", GUI, SRC_NAMES[s]);
        if (asset_image_load(jar, member, &srcs[s]) != 0)
            goto done;
        src_ok[s] = 1;
    }

    for (i = 0; i < sizeof(HUD_SPRITES) / sizeof(HUD_SPRITES[0]); i++) {
        const AssetImage *src = NULL;
        AssetImage crop = { 0 };
        for (s = 0; s < 4; s++) {
            if (strcmp(HUD_SPRITES[i].src, SRC_NAMES[s]) == 0) {
                src = &srcs[s];
                break;
            }
        }
        if (!src)
            goto done;
        if (asset_image_crop(src, HUD_SPRITES[i].x, HUD_SPRITES[i].y,
                             HUD_SPRITES[i].w, HUD_SPRITES[i].h, &crop) != 0)
            goto done;
        snprintf(table[tcount].name, sizeof(table[tcount].name), "%s", HUD_SPRITES[i].sym);
        table[tcount].w = HUD_SPRITES[i].w;
        table[tcount].h = HUD_SPRITES[i].h;
        table[tcount].off = (int)blen;
        if (blob_append(&blob, &blen, &bcap, crop.rgba,
                        (size_t)crop.w * crop.h * 4) != 0) {
            asset_image_free(&crop);
            goto done;
        }
        asset_image_free(&crop);
        tcount++;
    }

    {
        size_t expect = 0;
        for (i = 0; i < tcount; i++)
            expect += (size_t)table[i].w * (size_t)table[i].h * 4;
        if (expect != blen)
            goto done;
    }

    if (buf_puts(&b,
                 "/* GENERATED by make -C magma assets - DO NOT EDIT.\n"
                 " * Real MC 1.11.2 survival HUD gui sprites (widgets.png / icons.png),\n"
                 " * each stored as an RGBA run (R,G,B,A per texel) in one flat array. */\n"
                 "#ifndef MAGMA_HUD_ATLAS_H\n#define MAGMA_HUD_ATLAS_H\n\n") != 0)
        goto done;
    if (buf_printf(&b, "#define HUD_SPRITE_COUNT %zu\n\n", tcount) != 0)
        goto done;
    for (i = 0; i < tcount; i++) {
        if (buf_printf(&b, "#define HUD_%s %zu\n", table[i].name, i) != 0)
            goto done;
    }
    if (buf_puts(&b,
                 "\ntypedef struct { const char *name; int w, h, off; } HudSprite;\n"
                 "static const HudSprite HUD_SPRITES[HUD_SPRITE_COUNT] = {\n") != 0)
        goto done;
    for (i = 0; i < tcount; i++) {
        if (buf_printf(&b, "    { \"%s\", %d, %d, %d },\n",
                       table[i].name, table[i].w, table[i].h, table[i].off) != 0)
            goto done;
    }
    if (buf_printf(&b, "};\n\nstatic const unsigned char HUD_RGBA[%zu] = {\n", blen) != 0)
        goto done;
    if (emit_rgba_array(&b, blob, blen, "    ") != 0)
        goto done;
    if (buf_puts(&b, "};\n\n#endif /* MAGMA_HUD_ATLAS_H */\n") != 0)
        goto done;

    if (write_header_atomic(out_path, &b) != 0)
        goto done;
    rc = 0;
done:
    for (s = 0; s < 4; s++)
        if (src_ok[s])
            asset_image_free(&srcs[s]);
    free(blob);
    buf_free(&b);
    return rc;
}

/* ---- INVENTORY UI ---- */

static int build_inventory(AssetJar *jar, const char *out_path)
{
    static const char *const SYMS[] = {
        "EMPTY_HELMET", "EMPTY_CHESTPLATE", "EMPTY_LEGGINGS",
        "EMPTY_BOOTS", "EMPTY_SHIELD"
    };
    static const char *const FILES[] = {
        "empty_armor_slot_helmet.png",
        "empty_armor_slot_chestplate.png",
        "empty_armor_slot_leggings.png",
        "empty_armor_slot_boots.png",
        "empty_armor_slot_shield.png"
    };
    unsigned char pixels[5][16 * 16 * 4];
    size_t i;
    Buf b = { 0 };
    int rc = -1;
    char member[256];

    for (i = 0; i < 5; i++) {
        AssetImage img = { 0 };
        snprintf(member, sizeof(member),
                 "assets/minecraft/textures/items/%s", FILES[i]);
        if (asset_image_load(jar, member, &img) != 0)
            goto done;
        if (img.w != 16 || img.h != 16) {
            asset_image_free(&img);
            goto done;
        }
        memcpy(pixels[i], img.rgba, 16 * 16 * 4);
        asset_image_free(&img);
    }

    if (buf_puts(&b,
                 "/* GENERATED by make -C magma assets. */\n"
                 "#ifndef MAGMA_INVENTORY_UI_ATLAS_H\n"
                 "#define MAGMA_INVENTORY_UI_ATLAS_H\n\n"
                 "#define INVENTORY_UI_SPRITE_COUNT 5\n") != 0)
        goto done;
    for (i = 0; i < 5; i++) {
        if (buf_printf(&b, "#define INVENTORY_UI_%s %zu\n", SYMS[i], i) != 0)
            goto done;
    }
    if (buf_puts(&b, "\nstatic const unsigned char INVENTORY_UI_RGBA[5][16 * 16 * 4] = {\n") != 0)
        goto done;
    for (i = 0; i < 5; i++) {
        if (buf_printf(&b, "    { /* %s */\n", SYMS[i]) != 0)
            goto done;
        if (emit_rgba_array(&b, pixels[i], 16 * 16 * 4, "        ") != 0)
            goto done;
        if (buf_puts(&b, "    },\n") != 0)
            goto done;
    }
    if (buf_puts(&b, "};\n\n#endif /* MAGMA_INVENTORY_UI_ATLAS_H */\n") != 0)
        goto done;

    if (write_header_atomic(out_path, &b) != 0)
        goto done;
    rc = 0;
done:
    buf_free(&b);
    return rc;
}

/* ---- ITEM ---- */

typedef struct {
    int id;
    const char *member; /* under items/ or special */
} ItemSpec;

/* Sorted by id (same as Python after ITEM_SPRITES.sort). */
static const ItemSpec ITEM_SPRITES[] = {
    { 256, "iron_shovel.png" },
    { 257, "iron_pickaxe.png" },
    { 258, "iron_axe.png" },
    { 259, "flint_and_steel.png" },
    { 260, "apple.png" },
    { 261, "bow_standby.png" },
    { 262, "arrow.png" },
    { 263, "coal.png" },
    { 264, "diamond.png" },
    { 265, "iron_ingot.png" },
    { 266, "gold_ingot.png" },
    { 267, "iron_sword.png" },
    { 268, "wood_sword.png" },
    { 269, "wood_shovel.png" },
    { 270, "wood_pickaxe.png" },
    { 271, "wood_axe.png" },
    { 272, "stone_sword.png" },
    { 273, "stone_shovel.png" },
    { 274, "stone_pickaxe.png" },
    { 275, "stone_axe.png" },
    { 276, "diamond_sword.png" },
    { 277, "diamond_shovel.png" },
    { 278, "diamond_pickaxe.png" },
    { 279, "diamond_axe.png" },
    { 280, "stick.png" },
    { 283, "gold_sword.png" },
    { 287, "string.png" },
    { 288, "feather.png" },
    { 289, "gunpowder.png" },
    { 295, "seeds_wheat.png" },
    { 296, "wheat.png" },
    { 297, "bread.png" },
    { 318, "flint.png" },
    { 319, "porkchop_raw.png" },
    { 320, "porkchop_cooked.png" },
    { 331, "redstone_dust.png" },
    { 334, "leather.png" },
    { 338, "reeds.png" },
    { 352, "bone.png" },
    { 363, "beef_raw.png" },
    { 364, "beef_cooked.png" },
    { 368, "ender_pearl.png" },
    { 369, "blaze_rod.png" },
    { 377, "blaze_powder.png" },
    { 381, "ender_eye.png" },
    { 385, "fireball.png" },
    { 442, "empty_armor_slot_shield.png" },
    { 9000, "bow_pulling_0.png" },
    { 9001, "bow_pulling_1.png" },
    { 9002, "bow_pulling_2.png" },
    { 9003, "dragon_fireball.png" },
};

enum { ITEM_N = (int)(sizeof(ITEM_SPRITES) / sizeof(ITEM_SPRITES[0])), ITEM_TILE = 16 };

static int item_member_path(int id, const char *member, char *out, size_t out_sz)
{
    if (id == 9003)
        return snprintf(out, out_sz,
                        "assets/minecraft/textures/entity/enderdragon/dragon_fireball.png")
               < (int)out_sz
                   ? 0
                   : -1;
    if (id == 442)
        return snprintf(out, out_sz,
                        "assets/minecraft/textures/entity/shield_base_nopattern.png")
               < (int)out_sz
                   ? 0
                   : -1;
    return snprintf(out, out_sz, "assets/minecraft/textures/items/%s", member) < (int)out_sz
               ? 0
               : -1;
}

static void item_name_from_member(const char *member, char *out, size_t out_sz)
{
    size_t n = strlen(member);
    if (n > 4 && strcmp(member + n - 4, ".png") == 0)
        n -= 4;
    if (n >= out_sz)
        n = out_sz - 1;
    memcpy(out, member, n);
    out[n] = '\0';
}

static int build_item(AssetJar *jar, const char *out_path)
{
    AssetImage imgs[ITEM_N];
    char names[ITEM_N][48];
    int placed[ITEM_N][4]; /* x0,y0,x1,y1 */
    int order[ITEM_N];
    int shelves_y[ITEM_N], shelves_h[ITEM_N], shelves_x[ITEM_N];
    int n_shelves = 0;
    int canvas_w_needed = 0, canvas_h_needed = 0;
    int shelf_width;
    int max_w = 0;
    int canvas_w, canvas_h;
    AssetImage atlas = { 0 };
    int i, oi;
    Buf b = { 0 };
    int rc = -1;
    char member[256];

    memset(imgs, 0, sizeof(imgs));

    for (i = 0; i < ITEM_N; i++) {
        if (item_member_path(ITEM_SPRITES[i].id, ITEM_SPRITES[i].member, member,
                             sizeof(member)) != 0)
            goto done;
        if (asset_image_load(jar, member, &imgs[i]) != 0)
            goto done;
        item_name_from_member(ITEM_SPRITES[i].member, names[i], sizeof(names[i]));
        if ((imgs[i].w != ITEM_TILE || imgs[i].h != ITEM_TILE) &&
            ITEM_SPRITES[i].id != 442) {
            AssetImage resized = { 0 };
            if (asset_image_resize_nearest(&imgs[i], ITEM_TILE, ITEM_TILE, &resized) != 0)
                goto done;
            asset_image_free(&imgs[i]);
            imgs[i] = resized;
        }
        if (imgs[i].w > max_w)
            max_w = imgs[i].w;
    }

    shelf_width = 8 * ITEM_TILE;
    if (max_w > shelf_width)
        shelf_width = max_w;

    for (i = 0; i < ITEM_N; i++)
        order[i] = i;
    /* sort by (-height, item_id) */
    for (i = 0; i < ITEM_N; i++) {
        int j;
        for (j = i + 1; j < ITEM_N; j++) {
            int hi = imgs[order[i]].h, hj = imgs[order[j]].h;
            int idi = ITEM_SPRITES[order[i]].id, idj = ITEM_SPRITES[order[j]].id;
            if (hj > hi || (hj == hi && idj < idi)) {
                int t = order[i];
                order[i] = order[j];
                order[j] = t;
            }
        }
    }

    for (oi = 0; oi < ITEM_N; oi++) {
        int idx = order[oi];
        int w = imgs[idx].w, h = imgs[idx].h;
        int best = -1;
        int s, x, y;
        for (s = 0; s < n_shelves; s++) {
            if (shelves_h[s] >= h && shelves_x[s] + w <= shelf_width) {
                best = s;
                break;
            }
        }
        if (best < 0) {
            y = canvas_h_needed;
            shelves_y[n_shelves] = y;
            shelves_h[n_shelves] = h;
            shelves_x[n_shelves] = 0;
            best = n_shelves++;
            canvas_h_needed = y + h;
        }
        x = shelves_x[best];
        shelves_x[best] = x + w;
        if (shelves_x[best] > canvas_w_needed)
            canvas_w_needed = shelves_x[best];
        placed[idx][0] = x;
        placed[idx][1] = shelves_y[best];
        placed[idx][2] = x + w;
        placed[idx][3] = shelves_y[best] + h;
    }

    canvas_w = next_pow2(canvas_w_needed > shelf_width ? canvas_w_needed : shelf_width);
    canvas_h = next_pow2(canvas_h_needed);
    if (asset_image_new(&atlas, canvas_w, canvas_h) != 0)
        goto done;
    memset(atlas.rgba, 0, (size_t)canvas_w * canvas_h * 4);
    for (i = 0; i < ITEM_N; i++) {
        if (asset_image_paste(&atlas, &imgs[i], placed[i][0], placed[i][1]) != 0)
            goto done;
    }

    if (buf_puts(&b,
                 "/* GENERATED by make -C magma assets - DO NOT EDIT.\n"
                 " * Real MC 1.11.2 dropped-item sprites (16x16 native) grid-packed into one\n"
                 " * RGBA atlas. CR_ITEM_SPRITES is sorted ascending by vanilla item id.\n"
                 " * Byte order per texel: R,G,B,A (matches CrRgba memory layout). */\n"
                 "#ifndef MAGMA_ITEM_ATLAS_H\n#define MAGMA_ITEM_ATLAS_H\n\n") != 0)
        goto done;
    if (buf_printf(&b, "#define CR_ITEM_ATLAS_W %d\n", canvas_w) != 0)
        goto done;
    if (buf_printf(&b, "#define CR_ITEM_ATLAS_H %d\n", canvas_h) != 0)
        goto done;
    if (buf_printf(&b, "#define CR_ITEM_ATLAS_TILE %d\n", ITEM_TILE) != 0)
        goto done;
    if (buf_printf(&b, "#define CR_ITEM_SPRITE_COUNT %d\n\n", ITEM_N) != 0)
        goto done;
    if (buf_puts(&b,
                 "typedef struct { int id; const char *name; int x0, y0, x1, y1; }"
                 " CrItemSprite;\n"
                 "static const CrItemSprite CR_ITEM_SPRITES[CR_ITEM_SPRITE_COUNT] = {\n") != 0)
        goto done;
    /* table sorted by item id — ITEM_SPRITES already sorted */
    for (i = 0; i < ITEM_N; i++) {
        if (buf_printf(&b, "    { %d, \"%s\", %d, %d, %d, %d },\n",
                       ITEM_SPRITES[i].id, names[i],
                       placed[i][0], placed[i][1], placed[i][2], placed[i][3]) != 0)
            goto done;
    }
    if (buf_printf(&b, "};\n\nstatic const unsigned char CR_ITEM_ATLAS_RGBA[%d] = {\n",
                   canvas_w * canvas_h * 4) != 0)
        goto done;
    if (emit_rgba_array(&b, atlas.rgba, (size_t)canvas_w * canvas_h * 4, "    ") != 0)
        goto done;
    if (buf_puts(&b, "};\n\n#endif /* MAGMA_ITEM_ATLAS_H */\n") != 0)
        goto done;

    if (write_header_atomic(out_path, &b) != 0)
        goto done;
    rc = 0;
done:
    for (i = 0; i < ITEM_N; i++)
        asset_image_free(&imgs[i]);
    asset_image_free(&atlas);
    buf_free(&b);
    return rc;
}

/* ---- MOB ---- */

typedef struct {
    const char *name;
    const char *member;
} MobSpec;

/* MOB_SPRITES: name-sorted core then append-only (stable CR_MOB_* indices). */
static const MobSpec MOB_SPRITES[] = {
    /* sorted core (27) */
    { "arrow", "projectiles/arrow.png" },
    { "bat", "bat.png" },
    { "blaze", "blaze.png" },
    { "cave_spider", "spider/cave_spider.png" },
    { "chicken", "chicken.png" },
    { "cow", "cow/cow.png" },
    { "creeper", "creeper/creeper.png" },
    { "dragon", "enderdragon/dragon.png" },
    { "endercrystal", "endercrystal/endercrystal.png" },
    { "enderman", "enderman/enderman.png" },
    { "ghast", "ghast/ghast.png" },
    { "husk", "zombie/husk.png" },
    { "llama", "llama/llama_creamy.png" },
    { "magmacube", "slime/magmacube.png" },
    { "minecart", "minecart.png" },
    { "mooshroom", "cow/mooshroom.png" },
    { "pig", "pig/pig.png" },
    { "pigman", "zombie_pigman.png" },
    { "sheep", "sheep/sheep.png" },
    { "sheep_fur", "sheep/sheep_fur.png" },
    { "skeleton", "skeleton/skeleton.png" },
    { "spider", "spider/spider.png" },
    { "squid", "squid.png" },
    { "stray", "skeleton/stray.png" },
    { "witch", "witch.png" },
    { "wither_skeleton", "skeleton/wither_skeleton.png" },
    { "zombie", "zombie/zombie.png" },
    /* append-only */
    { "armorstand", "armorstand/wood.png" },
    { "experience_orb", "experience_orb.png" },
    { "slime", "slime/slime.png" },
    { "silverfish", "silverfish.png" },
    { "boat", "boat/boat_oak.png" },
    { "dragon_exploding", "enderdragon/dragon_exploding.png" },
    { "particles", "../particle/particles.png" },
    { "explosion", "explosion.png" },
    { "endercrystal_beam", "endercrystal/endercrystal_beam.png" },
    { "iron_layer_1", "../models/armor/iron_layer_1.png" },
    { "iron_layer_2", "../models/armor/iron_layer_2.png" },
    { "diamond_layer_1", "../models/armor/diamond_layer_1.png" },
    { "diamond_layer_2", "../models/armor/diamond_layer_2.png" },
};

enum { MOB_N = (int)(sizeof(MOB_SPRITES) / sizeof(MOB_SPRITES[0])), MOB_STABLE = 36 };

static int mob_path(const char *member, char *out, size_t out_sz)
{
    if (strncmp(member, "assets/", 7) == 0)
        return snprintf(out, out_sz, "%s", member) < (int)out_sz ? 0 : -1;
    if (strncmp(member, "../", 3) == 0)
        return snprintf(out, out_sz, "assets/minecraft/textures/%s", member + 3) <
                       (int)out_sz
                   ? 0
                   : -1;
    return snprintf(out, out_sz, "assets/minecraft/textures/entity/%s", member) <
                   (int)out_sz
               ? 0
               : -1;
}

/* Compare pack order keys: (-h, 0 if i<stable else 1, name if i<stable else i) */
static int mob_order_less(int ia, int ib, const AssetImage *imgs)
{
    int ha = imgs[ia].h, hb = imgs[ib].h;
    int ga = ia < MOB_STABLE ? 0 : 1;
    int gb = ib < MOB_STABLE ? 0 : 1;
    if (ha != hb)
        return ha > hb; /* taller first */
    if (ga != gb)
        return ga < gb;
    if (ga == 0)
        return strcmp(MOB_SPRITES[ia].name, MOB_SPRITES[ib].name) < 0;
    return ia < ib;
}

static int build_mob(AssetJar *jar, const char *out_path)
{
    AssetImage imgs[MOB_N];
    int order[MOB_N];
    int pos_x[MOB_N], pos_y[MOB_N];
    int max_w = 0;
    int canvas_w, canvas_h, used_h;
    int i, oi;
    int x, y, shelf_h;
    AssetImage atlas = { 0 };
    Buf b = { 0 };
    int rc = -1;
    char member[256];
    char uname[64];

    memset(imgs, 0, sizeof(imgs));

    for (i = 0; i < MOB_N; i++) {
        if (mob_path(MOB_SPRITES[i].member, member, sizeof(member)) != 0)
            goto done;
        if (asset_image_load(jar, member, &imgs[i]) != 0)
            goto done;
        if (imgs[i].w > max_w)
            max_w = imgs[i].w;
    }

    for (i = 0; i < MOB_N; i++)
        order[i] = i;
    for (i = 0; i < MOB_N; i++) {
        int j;
        for (j = i + 1; j < MOB_N; j++) {
            if (mob_order_less(order[j], order[i], imgs)) {
                int t = order[i];
                order[i] = order[j];
                order[j] = t;
            }
        }
    }

    canvas_w = next_pow2(max_w * 4);
    x = y = shelf_h = 0;
    for (oi = 0; oi < MOB_N; oi++) {
        int idx = order[oi];
        int w = imgs[idx].w, h = imgs[idx].h;
        if (x + w > canvas_w) {
            y += shelf_h;
            x = shelf_h = 0;
        }
        pos_x[idx] = x;
        pos_y[idx] = y;
        x += w;
        if (h > shelf_h)
            shelf_h = h;
    }
    used_h = y + shelf_h;
    canvas_h = next_pow2(used_h);

    if (asset_image_new(&atlas, canvas_w, canvas_h) != 0)
        goto done;
    memset(atlas.rgba, 0, (size_t)canvas_w * canvas_h * 4);
    for (i = 0; i < MOB_N; i++) {
        if (asset_image_paste(&atlas, &imgs[i], pos_x[i], pos_y[i]) != 0)
            goto done;
    }

    if (buf_puts(&b,
                 "/* GENERATED by make -C magma assets - DO NOT EDIT.\n"
                 " * Real MC 1.11.2 mob (entity) skins at NATIVE resolution, shelf-packed\n"
                 " * into one RGBA atlas. Each sprite records its atlas rect plus its native\n"
                 " * (w,h) so UVs can be computed in skin-texel space (vanilla ModelBox net).\n"
                 " * Byte order per texel: R,G,B,A (matches CrRgba memory layout). */\n"
                 "#ifndef MAGMA_MOB_ATLAS_H\n#define MAGMA_MOB_ATLAS_H\n\n") != 0)
        goto done;
    if (buf_printf(&b, "#define CR_MOB_ATLAS_W %d\n", canvas_w) != 0)
        goto done;
    if (buf_printf(&b, "#define CR_MOB_ATLAS_H %d\n", canvas_h) != 0)
        goto done;
    if (buf_puts(&b, "#define CR_MOB_ATLAS_TILE 64\n") != 0)
        goto done;
    if (buf_printf(&b, "#define CR_MOB_SPRITE_COUNT %d\n\n", MOB_N) != 0)
        goto done;
    for (i = 0; i < MOB_N; i++) {
        upper_copy(uname, sizeof(uname), MOB_SPRITES[i].name);
        if (buf_printf(&b, "#define CR_MOB_%s %d\n", uname, i) != 0)
            goto done;
    }
    if (buf_puts(&b,
                 "\ntypedef struct { const char *name; int x0, y0, x1, y1; int w, h; }"
                 " CrMobSprite;\n"
                 "static const CrMobSprite CR_MOB_SPRITES[CR_MOB_SPRITE_COUNT] = {\n") != 0)
        goto done;
    for (i = 0; i < MOB_N; i++) {
        int x0 = pos_x[i], y0 = pos_y[i];
        int w = imgs[i].w, h = imgs[i].h;
        if (buf_printf(&b, "    { \"%s\", %d, %d, %d, %d, %d, %d },\n",
                       MOB_SPRITES[i].name, x0, y0, x0 + w, y0 + h, w, h) != 0)
            goto done;
    }
    if (buf_printf(&b, "};\n\nstatic const unsigned char CR_MOB_ATLAS_RGBA[%d] = {\n",
                   canvas_w * canvas_h * 4) != 0)
        goto done;
    if (emit_rgba_array(&b, atlas.rgba, (size_t)canvas_w * canvas_h * 4, "    ") != 0)
        goto done;
    if (buf_puts(&b, "};\n\n#endif /* MAGMA_MOB_ATLAS_H */\n") != 0)
        goto done;

    if (write_header_atomic(out_path, &b) != 0)
        goto done;
    rc = 0;
done:
    for (i = 0; i < MOB_N; i++)
        asset_image_free(&imgs[i]);
    asset_image_free(&atlas);
    buf_free(&b);
    return rc;
}

/* ---- public entry ---- */

int asset_build_ui(AssetJar *jar, const char *out_dir, const char *only)
{
    char path[4096];
    int want_gui = 0, want_hand = 0, want_hud = 0;
    int want_inv = 0, want_item = 0, want_mob = 0;

    if (!jar || !out_dir)
        return -1;

    if (!only) {
        want_gui = want_hand = want_hud = want_inv = want_item = want_mob = 1;
    } else if (strcmp(only, "gui") == 0) {
        want_gui = 1;
    } else if (strcmp(only, "hand") == 0) {
        want_hand = 1;
    } else if (strcmp(only, "hud") == 0) {
        want_hud = 1;
    } else if (strcmp(only, "inventory") == 0) {
        want_inv = 1;
    } else if (strcmp(only, "item") == 0) {
        want_item = 1;
    } else if (strcmp(only, "mob") == 0) {
        want_mob = 1;
    } else {
        return -1; /* unknown name */
    }

    if (want_gui) {
        if (path_join(path, sizeof(path), out_dir, "gui_atlas.h") != 0)
            return -1;
        if (build_gui(jar, path) != 0)
            return -1;
    }
    if (want_hand) {
        if (path_join(path, sizeof(path), out_dir, "hand_atlas.h") != 0)
            return -1;
        if (build_hand(jar, path) != 0)
            return -1;
    }
    if (want_hud) {
        if (path_join(path, sizeof(path), out_dir, "hud_atlas.h") != 0)
            return -1;
        if (build_hud(jar, path) != 0)
            return -1;
    }
    if (want_inv) {
        if (path_join(path, sizeof(path), out_dir, "inventory_ui_atlas.h") != 0)
            return -1;
        if (build_inventory(jar, path) != 0)
            return -1;
    }
    if (want_item) {
        if (path_join(path, sizeof(path), out_dir, "item_atlas.h") != 0)
            return -1;
        if (build_item(jar, path) != 0)
            return -1;
    }
    if (want_mob) {
        if (path_join(path, sizeof(path), out_dir, "mob_atlas.h") != 0)
            return -1;
        if (build_mob(jar, path) != 0)
            return -1;
    }
    return 0;
}
