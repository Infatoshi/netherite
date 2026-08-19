/* test_build_ui.c - contract checks for asset_build_ui.
 * Usage: test_build_ui <minecraft-1.11.2.jar> <out_dir>
 * Writes headers into out_dir only (never over golden magma/assets headers). */
#include "build.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fails;

static void fail(const char *msg)
{
    fprintf(stderr, "FAIL: %s\n", msg);
    g_fails++;
}

static void failf(const char *fmt, int a)
{
    fprintf(stderr, "FAIL: ");
    fprintf(stderr, fmt, a);
    fprintf(stderr, "\n");
    g_fails++;
}

static char *read_file(const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    char *buf;
    long sz;
    size_t n;
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    sz = ftell(f);
    if (sz < 0) {
        fclose(f);
        return NULL;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    buf = (char *)malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    n = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[n] = '\0';
    if (out_len)
        *out_len = n;
    return buf;
}

static int parse_define_int(const char *text, const char *name, int *out)
{
    char key[128];
    const char *p;
    snprintf(key, sizeof(key), "#define %s ", name);
    p = strstr(text, key);
    if (!p)
        return -1;
    p += strlen(key);
    *out = (int)strtol(p, NULL, 10);
    return 0;
}

/* Parse "static const unsigned char NAME[N] = { ... };" into malloc'd bytes. */
static unsigned char *parse_rgba_array(const char *text, const char *name, size_t expect, size_t *got)
{
    char needle[160];
    const char *p, *q;
    unsigned char *px;
    size_t cap, n;
    char *end;

    snprintf(needle, sizeof(needle), "%s[", name);
    p = strstr(text, needle);
    if (!p)
        return NULL;
    p = strchr(p, '{');
    if (!p)
        return NULL;
    p++;
    q = strstr(p, "};");
    if (!q)
        return NULL;

    cap = expect ? expect : 1024;
    px = (unsigned char *)malloc(cap);
    if (!px)
        return NULL;
    n = 0;
    while (p < q) {
        while (p < q && (*p < '0' || *p > '9'))
            p++;
        if (p >= q)
            break;
        {
            long v = strtol(p, &end, 10);
            if (end == p)
                break;
            if (n >= cap) {
                size_t ncap = cap * 2;
                unsigned char *np = (unsigned char *)realloc(px, ncap);
                if (!np) {
                    free(px);
                    return NULL;
                }
                px = np;
                cap = ncap;
            }
            px[n++] = (unsigned char)v;
            p = end;
        }
    }
    if (got)
        *got = n;
    if (expect && n != expect) {
        free(px);
        return NULL;
    }
    return px;
}

static int expect_define(const char *text, const char *name, int want)
{
    int v;
    if (parse_define_int(text, name, &v) != 0) {
        fprintf(stderr, "FAIL: missing #define %s\n", name);
        g_fails++;
        return -1;
    }
    if (v != want) {
        fprintf(stderr, "FAIL: %s = %d want %d\n", name, v, want);
        g_fails++;
        return -1;
    }
    return 0;
}

static int rgba_eq(const unsigned char *a, const unsigned char *b, size_t n)
{
    return memcmp(a, b, n) == 0;
}

/* Sample a few texels from jar image vs generated run/atlas. */
static int check_jar_crop_pixels(AssetJar *jar, const char *member,
                                 int cx, int cy, int cw, int ch,
                                 const unsigned char *blob, int off)
{
    AssetImage img = { 0 };
    AssetImage crop = { 0 };
    size_t nbytes;
    int ok = -1;
    if (asset_image_load(jar, member, &img) != 0)
        return -1;
    if (asset_image_crop(&img, cx, cy, cw, ch, &crop) != 0) {
        asset_image_free(&img);
        return -1;
    }
    nbytes = (size_t)cw * (size_t)ch * 4;
    if (rgba_eq(crop.rgba, blob + off, nbytes))
        ok = 0;
    asset_image_free(&crop);
    asset_image_free(&img);
    return ok;
}

static int check_gui(AssetJar *jar, const char *path)
{
    char *text = read_file(path, NULL);
    unsigned char *rgba;
    size_t got = 0;
    if (!text) {
        fail("gui_atlas.h missing");
        return -1;
    }
    expect_define(text, "GUI_SPRITE_COUNT", 74);
    expect_define(text, "GUI_ITEM_ICON_COUNT", 65);
    expect_define(text, "GUI_INV_PANEL", 0);
    expect_define(text, "GUI_TABLE_PANEL", 1);
    expect_define(text, "GUI_FURNACE_PANEL", 2);
    expect_define(text, "GUI_FURNACE_FLAME", 3);
    expect_define(text, "GUI_FURNACE_ARROW", 4);
    expect_define(text, "GUI_FONT", 5);
    expect_define(text, "GUI_CHEST_PANEL", 6);
    expect_define(text, "GUI_EFFECT_PANEL", 7);
    expect_define(text, "GUI_EFFECT_ICONS", 8);

    rgba = parse_rgba_array(text, "GUI_RGBA", 651696, &got);
    if (!rgba) {
        fail("GUI_RGBA parse/size");
        free(text);
        return -1;
    }
    /* INV_PANEL first run: crop inventory.png 0,0 176x166 */
    if (check_jar_crop_pixels(
            jar, "assets/minecraft/textures/gui/container/inventory.png",
            0, 0, 176, 166, rgba, 0) != 0)
        fail("gui INV_PANEL pixels != jar");
    /* FONT at known offset from audit: 353008 */
    if (check_jar_crop_pixels(
            jar, "assets/minecraft/textures/font/ascii.png",
            0, 0, 128, 128, rgba, 353008) != 0)
        fail("gui FONT pixels != jar");
    free(rgba);
    free(text);
    return 0;
}

static int check_hand(AssetJar *jar, const char *path)
{
    char *text = read_file(path, NULL);
    unsigned char *steve, *alex;
    size_t got;
    AssetImage img = { 0 };
    if (!text) {
        fail("hand_atlas.h missing");
        return -1;
    }
    expect_define(text, "HAND_SKIN_W", 64);
    expect_define(text, "HAND_SKIN_H", 64);
    if (!strstr(text, "#define HAND_SKIN_RGBA HAND_SKIN_RGBA_STEVE"))
        fail("HAND_SKIN_RGBA alias");

    steve = parse_rgba_array(text, "HAND_SKIN_RGBA_STEVE", 16384, &got);
    alex = parse_rgba_array(text, "HAND_SKIN_RGBA_ALEX", 16384, &got);
    if (!steve || !alex) {
        fail("hand skin arrays");
        free(steve);
        free(alex);
        free(text);
        return -1;
    }
    if (asset_image_load(jar, "assets/minecraft/textures/entity/steve.png", &img) != 0) {
        fail("load steve.png");
    } else {
        size_t n = (size_t)img.w * img.h * 4;
        size_t cmp = n < 16384 ? n : 16384;
        if (memcmp(steve, img.rgba, cmp) != 0)
            fail("steve skin texels != jar");
        asset_image_free(&img);
    }
    if (asset_image_load(jar, "assets/minecraft/textures/entity/alex.png", &img) != 0) {
        fail("load alex.png");
    } else {
        size_t n = (size_t)img.w * img.h * 4;
        size_t cmp = n < 16384 ? n : 16384;
        if (memcmp(alex, img.rgba, cmp) != 0)
            fail("alex skin texels != jar");
        asset_image_free(&img);
    }
    free(steve);
    free(alex);
    free(text);
    return 0;
}

static int check_hud(AssetJar *jar, const char *path)
{
    char *text = read_file(path, NULL);
    unsigned char *rgba;
    size_t got;
    if (!text) {
        fail("hud_atlas.h missing");
        return -1;
    }
    expect_define(text, "HUD_SPRITE_COUNT", 39);
    expect_define(text, "HUD_HOTBAR", 0);
    expect_define(text, "HUD_SELECT", 1);
    expect_define(text, "HUD_CROSSHAIR", 2);
    expect_define(text, "HUD_BUTTON_HOVER", 38);

    rgba = parse_rgba_array(text, "HUD_RGBA", 123936, &got);
    if (!rgba) {
        fail("HUD_RGBA parse/size");
        free(text);
        return -1;
    }
    if (check_jar_crop_pixels(
            jar, "assets/minecraft/textures/gui/widgets.png",
            0, 0, 182, 22, rgba, 0) != 0)
        fail("hud HOTBAR pixels != jar");
    if (check_jar_crop_pixels(
            jar, "assets/minecraft/textures/gui/icons.png",
            0, 0, 15, 15, rgba, 18320) != 0)
        fail("hud CROSSHAIR pixels != jar");
    free(rgba);
    free(text);
    return 0;
}

static int check_inventory(AssetJar *jar, const char *path)
{
    char *text = read_file(path, NULL);
    const char *p;
    unsigned char *block;
    size_t n = 0, cap = 5 * 1024;
    char *end;
    AssetImage img = { 0 };
    if (!text) {
        fail("inventory_ui_atlas.h missing");
        return -1;
    }
    expect_define(text, "INVENTORY_UI_SPRITE_COUNT", 5);
    expect_define(text, "INVENTORY_UI_EMPTY_HELMET", 0);
    expect_define(text, "INVENTORY_UI_EMPTY_SHIELD", 4);

    p = strstr(text, "INVENTORY_UI_RGBA");
    if (!p) {
        fail("INVENTORY_UI_RGBA missing");
        free(text);
        return -1;
    }
    p = strchr(p, '{');
    if (!p) {
        fail("INVENTORY_UI_RGBA open");
        free(text);
        return -1;
    }
    block = (unsigned char *)malloc(cap);
    if (!block) {
        free(text);
        return -1;
    }
    /* Walk the whole array initializer; nested braces close each sprite. */
    {
        int depth = 0;
        const char *q = p;
        do {
            if (*q == '{')
                depth++;
            else if (*q == '}')
                depth--;
            q++;
        } while (*q && depth > 0);
        while (p < q && n < cap) {
            while (p < q && (*p < '0' || *p > '9'))
                p++;
            if (p >= q)
                break;
            {
                long v = strtol(p, &end, 10);
                if (end == p)
                    break;
                block[n++] = (unsigned char)v;
                p = end;
            }
        }
    }
    if (n != 5 * 16 * 16 * 4) {
        failf("inventory_ui rgba count %d", (int)n);
        free(block);
        free(text);
        return -1;
    }
    /* sprite 0 = empty helmet whole 16x16 */
    if (asset_image_load(jar,
                         "assets/minecraft/textures/items/empty_armor_slot_helmet.png",
                         &img) != 0) {
        fail("load empty helmet");
    } else {
        if (img.w != 16 || img.h != 16 || memcmp(block, img.rgba, 1024) != 0)
            fail("inventory empty helmet texels");
        asset_image_free(&img);
    }
    free(block);
    free(text);
    return 0;
}

/* Parse CR_ITEM_SPRITES entry for id 442. */
static int find_item_rect(const char *text, int id, int *x0, int *y0, int *x1, int *y1)
{
    char needle[32];
    const char *p;
    snprintf(needle, sizeof(needle), "{ %d, ", id);
    p = strstr(text, needle);
    if (!p)
        return -1;
    /* { id, "name", x0, y0, x1, y1 } */
    p = strchr(p, '"');
    if (!p)
        return -1;
    p = strchr(p + 1, '"');
    if (!p)
        return -1;
    p++;
    if (sscanf(p, ", %d, %d, %d, %d", x0, y0, x1, y1) != 4)
        return -1;
    return 0;
}

static int check_item(AssetJar *jar, const char *path)
{
    char *text = read_file(path, NULL);
    unsigned char *rgba;
    size_t got;
    int aw, ah, x0, y0, x1, y1;
    AssetImage shield = { 0 };
    int row, col;
    if (!text) {
        fail("item_atlas.h missing");
        return -1;
    }
    expect_define(text, "CR_ITEM_SPRITE_COUNT", 51);
    expect_define(text, "CR_ITEM_ATLAS_TILE", 16);
    if (parse_define_int(text, "CR_ITEM_ATLAS_W", &aw) != 0 ||
        parse_define_int(text, "CR_ITEM_ATLAS_H", &ah) != 0) {
        fail("item atlas dims");
        free(text);
        return -1;
    }
    if (aw != 128 || ah != 256) {
        fprintf(stderr, "FAIL: item atlas %dx%d want 128x256\n", aw, ah);
        g_fails++;
    }
    rgba = parse_rgba_array(text, "CR_ITEM_ATLAS_RGBA", (size_t)aw * ah * 4, &got);
    if (!rgba) {
        fail("CR_ITEM_ATLAS_RGBA");
        free(text);
        return -1;
    }
    if (find_item_rect(text, 442, &x0, &y0, &x1, &y1) != 0) {
        fail("item 442 rect missing");
    } else if (x1 - x0 != 64 || y1 - y0 != 64) {
        fprintf(stderr, "FAIL: shield rect %dx%d want 64x64\n", x1 - x0, y1 - y0);
        g_fails++;
    } else if (asset_image_load(
                   jar, "assets/minecraft/textures/entity/shield_base_nopattern.png",
                   &shield) != 0) {
        fail("load shield");
    } else {
        if (shield.w != 64 || shield.h != 64)
            fail("shield source not 64x64");
        for (row = 0; row < 64; row++) {
            const unsigned char *src = shield.rgba + (size_t)row * 64 * 4;
            const unsigned char *dst =
                rgba + ((size_t)(y0 + row) * (size_t)aw + (size_t)x0) * 4;
            if (memcmp(src, dst, 64 * 4) != 0) {
                fail("shield atlas texels != jar");
                break;
            }
        }
        /* also sample a 16x16 item: wood_pickaxe id 270 */
        {
            int px0, py0, px1, py1;
            AssetImage pick = { 0 };
            if (find_item_rect(text, 270, &px0, &py0, &px1, &py1) != 0) {
                fail("item 270 rect");
            } else if (px1 - px0 != 16 || py1 - py0 != 16) {
                fail("item 270 not 16x16");
            } else if (asset_image_load(
                           jar, "assets/minecraft/textures/items/wood_pickaxe.png",
                           &pick) == 0) {
                for (row = 0; row < 16; row++) {
                    const unsigned char *src = pick.rgba + (size_t)row * 16 * 4;
                    const unsigned char *dst =
                        rgba + ((size_t)(py0 + row) * (size_t)aw + (size_t)px0) * 4;
                    if (memcmp(src, dst, 16 * 4) != 0) {
                        fail("wood_pickaxe atlas texels != jar");
                        break;
                    }
                }
                asset_image_free(&pick);
            }
        }
        (void)col;
        asset_image_free(&shield);
    }
    free(rgba);
    free(text);
    return 0;
}

static int find_mob_rect(const char *text, const char *name,
                        int *x0, int *y0, int *x1, int *y1, int *w, int *h)
{
    char needle[80];
    const char *p;
    snprintf(needle, sizeof(needle), "{ \"%s\", ", name);
    p = strstr(text, needle);
    if (!p)
        return -1;
    if (sscanf(p + 2, "\"%*[^\"]\", %d, %d, %d, %d, %d, %d",
               x0, y0, x1, y1, w, h) != 6) {
        /* sscanf with skip may fail; parse manually after name */
        p = strchr(p, '"');
        if (!p)
            return -1;
        p = strchr(p + 1, '"');
        if (!p)
            return -1;
        p++;
        if (sscanf(p, ", %d, %d, %d, %d, %d, %d", x0, y0, x1, y1, w, h) != 6)
            return -1;
    }
    return 0;
}

static int check_mob(AssetJar *jar, const char *path)
{
    char *text = read_file(path, NULL);
    unsigned char *rgba;
    size_t got;
    int aw, ah;
    static const char *const required[] = {
        "pigman", "husk", "stray", "cave_spider", "mooshroom",
        "zombie", "skeleton", "spider", "cow", "sheep", "sheep_fur",
        "slime", "dragon", "dragon_exploding", "particles", "explosion",
        "endercrystal_beam", "iron_layer_1", "diamond_layer_2"
    };
    size_t ri;
    if (!text) {
        fail("mob_atlas.h missing");
        return -1;
    }
    expect_define(text, "CR_MOB_SPRITE_COUNT", 40);
    expect_define(text, "CR_MOB_ATLAS_TILE", 64);
    expect_define(text, "CR_MOB_ARROW", 0);
    expect_define(text, "CR_MOB_ZOMBIE", 26);
    expect_define(text, "CR_MOB_ARMORSTAND", 27);
    expect_define(text, "CR_MOB_EXPERIENCE_ORB", 28);
    expect_define(text, "CR_MOB_SLIME", 29);
    expect_define(text, "CR_MOB_SILVERFISH", 30);
    expect_define(text, "CR_MOB_BOAT", 31);
    expect_define(text, "CR_MOB_DRAGON_EXPLODING", 32);
    expect_define(text, "CR_MOB_PARTICLES", 33);
    expect_define(text, "CR_MOB_EXPLOSION", 34);
    expect_define(text, "CR_MOB_ENDERCRYSTAL_BEAM", 35);
    expect_define(text, "CR_MOB_IRON_LAYER_1", 36);
    expect_define(text, "CR_MOB_DIAMOND_LAYER_2", 39);

    if (parse_define_int(text, "CR_MOB_ATLAS_W", &aw) != 0 ||
        parse_define_int(text, "CR_MOB_ATLAS_H", &ah) != 0) {
        fail("mob atlas dims");
        free(text);
        return -1;
    }
    if (aw != 1024 || ah != 512) {
        fprintf(stderr, "FAIL: mob atlas %dx%d want 1024x512\n", aw, ah);
        g_fails++;
    }
    rgba = parse_rgba_array(text, "CR_MOB_ATLAS_RGBA", (size_t)aw * ah * 4, &got);
    if (!rgba) {
        fail("CR_MOB_ATLAS_RGBA");
        free(text);
        return -1;
    }

    for (ri = 0; ri < sizeof(required) / sizeof(required[0]); ri++) {
        char def[80];
        snprintf(def, sizeof(def), "CR_MOB_");
        {
            size_t k;
            size_t base = strlen(def);
            for (k = 0; required[ri][k] && base + k + 1 < sizeof(def); k++) {
                char c = required[ri][k];
                def[base + k] = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
            }
            def[base + k] = '\0';
        }
        if (!strstr(text, def)) {
            fprintf(stderr, "FAIL: missing %s\n", def);
            g_fails++;
        }
    }

    /* Native texel check: dragon (256x256), arrow (32x32), endercrystal_beam (16x256) */
    {
        struct {
            const char *name;
            const char *member;
        } samples[] = {
            { "dragon", "assets/minecraft/textures/entity/enderdragon/dragon.png" },
            { "arrow", "assets/minecraft/textures/entity/projectiles/arrow.png" },
            { "endercrystal_beam",
              "assets/minecraft/textures/entity/endercrystal/endercrystal_beam.png" },
            { "particles", "assets/minecraft/textures/particle/particles.png" },
            { "zombie", "assets/minecraft/textures/entity/zombie/zombie.png" },
        };
        size_t si;
        for (si = 0; si < sizeof(samples) / sizeof(samples[0]); si++) {
            int x0, y0, x1, y1, nw, nh, row;
            AssetImage img = { 0 };
            if (find_mob_rect(text, samples[si].name, &x0, &y0, &x1, &y1, &nw, &nh) != 0) {
                fprintf(stderr, "FAIL: mob rect %s\n", samples[si].name);
                g_fails++;
                continue;
            }
            if (asset_image_load(jar, samples[si].member, &img) != 0) {
                fprintf(stderr, "FAIL: load %s\n", samples[si].member);
                g_fails++;
                continue;
            }
            if (img.w != nw || img.h != nh || x1 - x0 != nw || y1 - y0 != nh) {
                fprintf(stderr, "FAIL: %s size jar %dx%d atlas native %dx%d rect %dx%d\n",
                        samples[si].name, img.w, img.h, nw, nh, x1 - x0, y1 - y0);
                g_fails++;
                asset_image_free(&img);
                continue;
            }
            for (row = 0; row < nh; row++) {
                const unsigned char *src = img.rgba + (size_t)row * (size_t)nw * 4;
                const unsigned char *dst =
                    rgba + ((size_t)(y0 + row) * (size_t)aw + (size_t)x0) * 4;
                if (memcmp(src, dst, (size_t)nw * 4) != 0) {
                    fprintf(stderr, "FAIL: %s texels row %d != jar\n",
                            samples[si].name, row);
                    g_fails++;
                    break;
                }
            }
            asset_image_free(&img);
        }
    }

    free(rgba);
    free(text);
    return 0;
}

static int check_unknown_only(AssetJar *jar, const char *out_dir)
{
    if (asset_build_ui(jar, out_dir, "not_a_real_name") == 0) {
        fail("unknown only name should fail");
        return -1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    AssetJar *jar;
    char path[4096];
    const char *jar_path;
    const char *out_dir;

    if (argc != 3) {
        fprintf(stderr, "usage: %s <minecraft-1.11.2.jar> <out_dir>\n", argv[0]);
        return 2;
    }
    jar_path = argv[1];
    out_dir = argv[2];

    jar = asset_jar_open(jar_path);
    if (!jar) {
        fprintf(stderr, "FAIL: asset_jar_open(%s)\n", jar_path);
        return 1;
    }

    check_unknown_only(jar, out_dir);

    if (asset_build_ui(jar, out_dir, NULL) != 0) {
        fprintf(stderr, "FAIL: asset_build_ui all\n");
        asset_jar_close(jar);
        return 1;
    }

    /* only= one name still succeeds for hand */
    if (asset_build_ui(jar, out_dir, "hand") != 0) {
        fail("only=hand");
    }

    snprintf(path, sizeof(path), "%s/gui_atlas.h", out_dir);
    check_gui(jar, path);
    snprintf(path, sizeof(path), "%s/hand_atlas.h", out_dir);
    check_hand(jar, path);
    snprintf(path, sizeof(path), "%s/hud_atlas.h", out_dir);
    check_hud(jar, path);
    snprintf(path, sizeof(path), "%s/inventory_ui_atlas.h", out_dir);
    check_inventory(jar, path);
    snprintf(path, sizeof(path), "%s/item_atlas.h", out_dir);
    check_item(jar, path);
    snprintf(path, sizeof(path), "%s/mob_atlas.h", out_dir);
    check_mob(jar, path);

    asset_jar_close(jar);

    if (g_fails) {
        fprintf(stderr, "test_build_ui: %d FAILURES\n", g_fails);
        return 1;
    }
    printf("test_build_ui: PASS\n");
    return 0;
}
