#include "build.h"
#include "image.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- emit helpers ---- */

static int atomic_write(const char *out_dir, const char *filename, const char *body) {
    char path[1024];
    char tmp[1100];
    FILE *fp;
    size_t n;

    if (!out_dir || !filename || !body)
        return -1;
    if (snprintf(path, sizeof(path), "%s/%s", out_dir, filename) >= (int)sizeof(path))
        return -1;
    if (snprintf(tmp, sizeof(tmp), "%s/.%s.tmp", out_dir, filename) >= (int)sizeof(tmp))
        return -1;
    fp = fopen(tmp, "wb");
    if (!fp)
        return -1;
    n = strlen(body);
    if (fwrite(body, 1, n, fp) != n) {
        fclose(fp);
        remove(tmp);
        return -1;
    }
    if (fflush(fp) != 0) {
        fclose(fp);
        remove(tmp);
        return -1;
    }
    if (fclose(fp) != 0) {
        remove(tmp);
        return -1;
    }
    if (rename(tmp, path) != 0) {
        remove(tmp);
        return -1;
    }
    return 0;
}

typedef struct {
    char *buf;
    size_t len;
    size_t cap;
} Buf;

static int buf_init(Buf *b) {
    b->cap = 4096;
    b->len = 0;
    b->buf = (char *)malloc(b->cap);
    if (!b->buf)
        return -1;
    b->buf[0] = '\0';
    return 0;
}

static void buf_free(Buf *b) {
    free(b->buf);
    b->buf = NULL;
    b->len = b->cap = 0;
}

static int buf_reserve(Buf *b, size_t need) {
    char *nbuf;
    size_t ncap;
    if (b->len + need + 1 <= b->cap)
        return 0;
    ncap = b->cap;
    while (b->len + need + 1 > ncap)
        ncap *= 2;
    nbuf = (char *)realloc(b->buf, ncap);
    if (!nbuf)
        return -1;
    b->buf = nbuf;
    b->cap = ncap;
    return 0;
}

static int buf_printf(Buf *b, const char *fmt, ...) {
    va_list ap;
    int n;
    for (;;) {
        size_t avail = b->cap - b->len;
        va_list aq;
        va_start(ap, fmt);
        va_copy(aq, ap);
        n = vsnprintf(b->buf + b->len, avail, fmt, aq);
        va_end(aq);
        va_end(ap);
        if (n < 0)
            return -1;
        if ((size_t)n < avail) {
            b->len += (size_t)n;
            return 0;
        }
        if (buf_reserve(b, (size_t)n + 1) != 0)
            return -1;
    }
}

static int emit_u8_lines(Buf *b, const unsigned char *px, size_t n, int per_line) {
    size_t i;
    for (i = 0; i < n; i++) {
        if (i % (size_t)per_line == 0) {
            if (buf_printf(b, "    ") != 0)
                return -1;
        }
        if (buf_printf(b, "%d", (int)px[i]) != 0)
            return -1;
        if (i + 1 < n) {
            if (buf_printf(b, ",") != 0)
                return -1;
            if ((i + 1) % (size_t)per_line == 0) {
                if (buf_printf(b, "\n") != 0)
                    return -1;
            }
        } else {
            if (buf_printf(b, ",\n") != 0)
                return -1;
        }
    }
    return 0;
}

/* ---- sprite name list (matches build_atlas.py) ---- */

static const char *SPRITE_BASE[] = {
    "bedrock",
    "coarse_dirt",
    "dirt",
    "dirt_podzol_side",
    "dirt_podzol_top",
    "grass_side",
    "grass_side_overlay",
    "grass_top",
    "gravel",
    "hardened_clay",
    "hardened_clay_stained_white",
    "ice",
    "mycelium_side",
    "mycelium_top",
    "red_sandstone_bottom",
    "red_sandstone_normal",
    "red_sandstone_top",
    "sand",
    "sandstone_bottom",
    "sandstone_normal",
    "sandstone_top",
    "snow",
    "stone",
    "water_flow",
    "water_still",
    "waterlily",
    "leaves_oak",
    "leaves_birch",
    "leaves_spruce",
    "leaves_acacia",
    "leaves_jungle",
    "leaves_big_oak",
    "log_oak",
    "log_oak_top",
    "log_birch",
    "log_birch_top",
    "log_spruce",
    "log_spruce_top",
    "log_acacia",
    "log_acacia_top",
    "log_jungle",
    "log_jungle_top",
    "log_big_oak",
    "log_big_oak_top",
    "tallgrass",
    "fern",
    "deadbush",
    "flower_dandelion",
    "flower_rose",
    "flower_blue_orchid",
    "flower_allium",
    "flower_houstonia",
    "flower_tulip_red",
    "flower_tulip_orange",
    "flower_tulip_white",
    "flower_tulip_pink",
    "flower_oxeye_daisy",
    "reeds",
    "double_plant_grass_bottom",
    "double_plant_grass_top",
    "double_plant_fern_bottom",
    "double_plant_fern_top",
    "double_plant_sunflower_bottom",
    "double_plant_sunflower_top",
    "double_plant_sunflower_front",
    "double_plant_sunflower_back",
    "double_plant_syringa_bottom",
    "double_plant_syringa_top",
    "double_plant_rose_bottom",
    "double_plant_rose_top",
    "double_plant_paeonia_bottom",
    "double_plant_paeonia_top",
    "mushroom_brown",
    "mushroom_red",
    "vine",
    "glass",
    "lava_flow",
    "lava_still",
    "planks_oak",
    "rail_normal",
    "furnace_front_off",
    "furnace_side",
    "furnace_top",
    "hopper_inside",
    "hopper_outside",
    "hopper_top",
    "tnt_bottom",
    "tnt_side",
    "tnt_top",
    "crafting_table_top",
    "crafting_table_front",
    "crafting_table_side",
    "cobblestone",
    "cobblestone_mossy",
    "stone_granite",
    "stone_granite_smooth",
    "stone_diorite",
    "stone_diorite_smooth",
    "stone_andesite",
    "stone_andesite_smooth",
    "coal_ore",
    "iron_ore",
    "gold_ore",
    "redstone_ore",
    "diamond_ore",
    "lapis_ore",
    "emerald_ore",
    "clay",
    "obsidian",
    "netherrack",
    "nether_brick",
    "portal",
    "end_stone",
    "glowstone",
    "soul_sand",
    "fire_layer_0",
    "fire_layer_1",
    "magma",
    "quartz_ore",
    "endframe_side",
    "endframe_top",
    "endframe_eye",
    "iron_bars",
    "torch_on",
    "bone_block_side",
    "bone_block_top",
    "cactus_side",
    "cactus_top",
    "cactus_bottom",
    "pumpkin_side",
    "pumpkin_top",
    "melon_side",
    "melon_top",
    "mushroom_block_skin_brown",
    "mushroom_block_skin_red",
    "mushroom_block_inside",
    "mob_spawner",
    "destroy_stage_0",
    "destroy_stage_1",
    "destroy_stage_2",
    "destroy_stage_3",
    "destroy_stage_4",
    "destroy_stage_5",
    "destroy_stage_6",
    "destroy_stage_7",
    "destroy_stage_8",
    "destroy_stage_9",
};

static const char *SPRITE_APPEND[] = {
    "ice_packed", "slime", "web", "end_portal", "tnt_bottom", "tnt_side", "tnt_top",
    "brick", "quartz_block_bottom", "quartz_block_side", "quartz_block_top",
    "stone_slab_side", "stone_slab_top", "stonebrick", "glass_pane_top", "trapdoor",
    "ladder",
};

static int cmp_cstr(const void *a, const void *b) {
    const char *const *sa = (const char *const *)a;
    const char *const *sb = (const char *const *)b;
    return strcmp(*sa, *sb);
}

static int is_native_flow(const char *name) {
    return strcmp(name, "water_flow") == 0 || strcmp(name, "lava_flow") == 0;
}

static int load_sprite(AssetJar *jar, const char *name, AssetImage *out) {
    char member[256];
    AssetImage img, frame;
    int w, h;

    memset(&img, 0, sizeof(img));
    memset(&frame, 0, sizeof(frame));
    memset(out, 0, sizeof(*out));

    if (strcmp(name, "end_portal") == 0) {
        if (snprintf(member, sizeof(member),
                     "assets/minecraft/textures/entity/end_portal.png") >=
            (int)sizeof(member))
            return -1;
    } else {
        if (snprintf(member, sizeof(member),
                     "assets/minecraft/textures/blocks/%s.png", name) >=
            (int)sizeof(member))
            return -1;
    }
    if (asset_image_load(jar, member, &img) != 0)
        return -1;
    w = img.w;
    h = img.h;
    if (h > w && h % w == 0) {
        if (asset_image_crop(&img, 0, 0, w, w, &frame) != 0) {
            asset_image_free(&img);
            return -1;
        }
        asset_image_free(&img);
        img = frame;
        memset(&frame, 0, sizeof(frame));
        w = img.w;
        h = img.h;
    }
    if (is_native_flow(name)) {
        if (w != 32 || h != 32) {
            asset_image_free(&img);
            return -1;
        }
        *out = img;
        return 0;
    }
    if (w != 16 || h != 16) {
        if (asset_image_resize_nearest(&img, 16, 16, &frame) != 0) {
            asset_image_free(&img);
            return -1;
        }
        asset_image_free(&img);
        *out = frame;
        return 0;
    }
    *out = img;
    return 0;
}

static void upper_copy(const char *in, char *out, size_t out_sz) {
    size_t i;
    for (i = 0; in[i] && i + 1 < out_sz; i++) {
        char c = in[i];
        if (c >= 'a' && c <= 'z')
            c = (char)(c - 'a' + 'A');
        out[i] = c;
    }
    out[i < out_sz ? i : out_sz - 1] = '\0';
}

static int gen_atlas(AssetJar *jar, const char *out_dir) {
    const char *names[256];
    int n = 0;
    int i, side_tiles, atlas_dim;
    AssetImage atlas;
    Buf b;
    int x0[256], y0[256], x1[256], y1[256];

    /* sorted unique base */
    {
        const char *tmp[256];
        int nt = (int)(sizeof(SPRITE_BASE) / sizeof(SPRITE_BASE[0]));
        for (i = 0; i < nt; i++)
            tmp[i] = SPRITE_BASE[i];
        qsort(tmp, (size_t)nt, sizeof(tmp[0]), cmp_cstr);
        for (i = 0; i < nt; i++) {
            if (i == 0 || strcmp(tmp[i], tmp[i - 1]) != 0)
                names[n++] = tmp[i];
        }
    }
    for (i = 0; i < (int)(sizeof(SPRITE_APPEND) / sizeof(SPRITE_APPEND[0])); i++)
        names[n++] = SPRITE_APPEND[i];

    side_tiles = 1;
    while (side_tiles * side_tiles < n)
        side_tiles <<= 1;
    atlas_dim = side_tiles * 16;
    if (asset_image_new(&atlas, atlas_dim, atlas_dim) != 0)
        return -1;

    for (i = 0; i < n; i++) {
        AssetImage spr;
        int tx, ty;
        if (load_sprite(jar, names[i], &spr) != 0) {
            asset_image_free(&atlas);
            return -1;
        }
        if (is_native_flow(names[i])) {
            int idx = strcmp(names[i], "water_flow") == 0 ? 0 : 1;
            /* NATIVE_FLOW_SPRITES index: water_flow=0, lava_flow=1 */
            if (strcmp(names[i], "water_flow") == 0)
                idx = 0;
            else
                idx = 1;
            tx = idx * 32;
            ty = atlas_dim - 32;
        } else {
            tx = (i % side_tiles) * 16;
            ty = (i / side_tiles) * 16;
            if (ty + 16 > atlas_dim - 32) {
                asset_image_free(&spr);
                asset_image_free(&atlas);
                return -1;
            }
        }
        if (asset_image_paste(&atlas, &spr, tx, ty) != 0) {
            asset_image_free(&spr);
            asset_image_free(&atlas);
            return -1;
        }
        x0[i] = tx;
        y0[i] = ty;
        x1[i] = tx + spr.w;
        y1[i] = ty + spr.h;
        asset_image_free(&spr);
    }

    if (buf_init(&b) != 0) {
        asset_image_free(&atlas);
        return -1;
    }
    if (buf_printf(&b,
                   "/* GENERATED by make -C magma assets - DO NOT EDIT.\n"
                   " * Real MC 1.11.2 block textures stitched into one RGBA atlas.\n"
                   " * Byte order per texel: R,G,B,A (matches CrRgba memory layout). */\n"
                   "#ifndef MAGMA_ATLAS_GEN_H\n#define MAGMA_ATLAS_GEN_H\n\n"
                   "#define CR_ATLAS_W %d\n"
                   "#define CR_ATLAS_H %d\n"
                   "#define CR_ATLAS_TILE %d\n"
                   "#define CR_ATLAS_SPRITE_COUNT %d\n\n",
                   atlas_dim, atlas_dim, 16, n) != 0)
        goto fail;
    for (i = 0; i < n; i++) {
        char up[128];
        upper_copy(names[i], up, sizeof(up));
        if (buf_printf(&b, "#define CR_SPRITE_%s %d\n", up, i) != 0)
            goto fail;
    }
    if (buf_printf(&b, "\n") != 0)
        goto fail;
    if (buf_printf(&b,
                   "typedef struct { const char *name; int x0, y0, x1, y1; } CrAtlasSprite;\n"
                   "static const CrAtlasSprite CR_ATLAS_SPRITES[CR_ATLAS_SPRITE_COUNT] = {\n") != 0)
        goto fail;
    for (i = 0; i < n; i++) {
        if (buf_printf(&b, "    { \"%s\", %d, %d, %d, %d },\n", names[i], x0[i], y0[i],
                       x1[i], y1[i]) != 0)
            goto fail;
    }
    if (buf_printf(&b, "};\n\n") != 0)
        goto fail;
    {
        size_t pxn = (size_t)atlas_dim * (size_t)atlas_dim * 4u;
        if (buf_printf(&b, "static const unsigned char CR_ATLAS_RGBA[%zu] = {\n", pxn) != 0)
            goto fail;
        if (emit_u8_lines(&b, atlas.rgba, pxn, 16) != 0)
            goto fail;
    }
    if (buf_printf(&b, "};\n\n#endif /* MAGMA_ATLAS_GEN_H */\n") != 0)
        goto fail;

    if (atomic_write(out_dir, "atlas_gen.h", b.buf) != 0)
        goto fail;
    buf_free(&b);
    asset_image_free(&atlas);
    return 0;
fail:
    buf_free(&b);
    asset_image_free(&atlas);
    return -1;
}

static int gen_colormap(AssetJar *jar, const char *out_dir) {
    static const char *names[] = {"grass", "foliage"};
    static const char *cnames[] = {"CR_GRASS_COLORMAP", "CR_FOLIAGE_COLORMAP"};
    Buf b;
    int k;

    if (buf_init(&b) != 0)
        return -1;
    if (buf_printf(&b,
                   "/* GENERATED by make -C magma assets - DO NOT EDIT.\n"
                   " * Real MC 1.11.2 biome colormaps (grass.png, foliage.png), 256x256.\n"
                   " * Row-major 0xRRGGBB; index [j<<8 | i] exactly as ColorizerGrass/\n"
                   " * ColorizerFoliage.getGrassColor/getFoliageColor do. */\n"
                   "#ifndef MAGMA_COLORMAP_GEN_H\n#define MAGMA_COLORMAP_GEN_H\n\n"
                   "#define CR_COLORMAP_DIM 256\n\n") != 0)
        goto fail;

    for (k = 0; k < 2; k++) {
        char member[128];
        AssetImage img;
        int i, count;
        if (snprintf(member, sizeof(member),
                     "assets/minecraft/textures/colormap/%s.png", names[k]) >=
            (int)sizeof(member))
            goto fail;
        if (asset_image_load(jar, member, &img) != 0)
            goto fail;
        if (img.w != 256 || img.h != 256) {
            asset_image_free(&img);
            goto fail;
        }
        count = 256 * 256;
        if (buf_printf(&b, "static const unsigned int %s[%d] = {\n", cnames[k], count) != 0) {
            asset_image_free(&img);
            goto fail;
        }
        for (i = 0; i < count; i++) {
            unsigned char *p = img.rgba + (size_t)i * 4u;
            unsigned int v = ((unsigned int)p[0] << 16) | ((unsigned int)p[1] << 8) |
                             (unsigned int)p[2];
            if (i % 12 == 0) {
                if (buf_printf(&b, "    ") != 0) {
                    asset_image_free(&img);
                    goto fail;
                }
            }
            if (buf_printf(&b, "0x%06X", v) != 0) {
                asset_image_free(&img);
                goto fail;
            }
            if (buf_printf(&b, ",") != 0) {
                asset_image_free(&img);
                goto fail;
            }
            if ((i + 1) % 12 == 0 || i + 1 == count) {
                if (buf_printf(&b, "\n") != 0) {
                    asset_image_free(&img);
                    goto fail;
                }
            }
        }
        if (buf_printf(&b, "};\n\n") != 0) {
            asset_image_free(&img);
            goto fail;
        }
        asset_image_free(&img);
    }
    if (buf_printf(&b, "#endif /* MAGMA_COLORMAP_GEN_H */\n") != 0)
        goto fail;
    if (atomic_write(out_dir, "colormap_gen.h", b.buf) != 0)
        goto fail;
    buf_free(&b);
    return 0;
fail:
    buf_free(&b);
    return -1;
}

static int gen_loading(AssetJar *jar, const char *out_dir) {
    AssetImage img;
    Buf b;
    size_t n;

    if (asset_image_load(jar, "assets/minecraft/textures/gui/options_background.png",
                         &img) != 0)
        return -1;
    if (img.w != 16 || img.h != 16) {
        asset_image_free(&img);
        return -1;
    }
    if (buf_init(&b) != 0) {
        asset_image_free(&img);
        return -1;
    }
    n = 16u * 16u * 4u;
    if (buf_printf(&b,
                   "/* GENERATED by make -C magma assets - DO NOT EDIT. */\n"
                   "#ifndef MAGMA_ASSETS_LOADING_BG_H\n"
                   "#define MAGMA_ASSETS_LOADING_BG_H\n\n"
                   "static const unsigned char CR_LOADING_BG_RGBA[1024] = {\n") != 0)
        goto fail;
    if (emit_u8_lines(&b, img.rgba, n, 16) != 0)
        goto fail;
    if (buf_printf(&b, "};\n\n#endif /* MAGMA_ASSETS_LOADING_BG_H */\n") != 0)
        goto fail;
    if (atomic_write(out_dir, "loading_bg.h", b.buf) != 0)
        goto fail;
    buf_free(&b);
    asset_image_free(&img);
    return 0;
fail:
    buf_free(&b);
    asset_image_free(&img);
    return -1;
}

static int gen_portal(AssetJar *jar, const char *out_dir) {
    AssetImage img;
    Buf b;
    int frames, f;

    if (asset_image_load(jar, "assets/minecraft/textures/blocks/portal.png", &img) != 0)
        return -1;
    if (img.w != 16 || img.h % img.w != 0) {
        asset_image_free(&img);
        return -1;
    }
    frames = img.h / img.w;
    if (buf_init(&b) != 0) {
        asset_image_free(&img);
        return -1;
    }
    if (buf_printf(&b,
                   "/* GENERATED by make -C magma assets - DO NOT EDIT.\n"
                   " * MC 1.11.2 blocks/portal.png animation frames. */\n"
                   "#ifndef MAGMA_ASSETS_PORTAL_TEX_H\n"
                   "#define MAGMA_ASSETS_PORTAL_TEX_H\n\n"
                   "#define CR_PORTAL_TEX_FRAMES %d\n"
                   "#define CR_PORTAL_TEX_W %d\n"
                   "#define CR_PORTAL_TEX_H %d\n\n"
                   "static const unsigned char "
                   "CR_PORTAL_TEX[CR_PORTAL_TEX_FRAMES][1024] = {\n",
                   frames, img.w, img.w) != 0)
        goto fail;
    for (f = 0; f < frames; f++) {
        AssetImage tile;
        if (asset_image_crop(&img, 0, f * img.w, img.w, img.w, &tile) != 0)
            goto fail;
        if (buf_printf(&b, "  {\n") != 0) {
            asset_image_free(&tile);
            goto fail;
        }
        if (emit_u8_lines(&b, tile.rgba, 1024, 16) != 0) {
            asset_image_free(&tile);
            goto fail;
        }
        if (buf_printf(&b, "  },\n") != 0) {
            asset_image_free(&tile);
            goto fail;
        }
        asset_image_free(&tile);
    }
    if (buf_printf(&b, "};\n\n#endif /* MAGMA_ASSETS_PORTAL_TEX_H */\n") != 0)
        goto fail;
    if (atomic_write(out_dir, "portal_tex.h", b.buf) != 0)
        goto fail;
    buf_free(&b);
    asset_image_free(&img);
    return 0;
fail:
    buf_free(&b);
    asset_image_free(&img);
    return -1;
}

static int emit_rgba_array(Buf *b, const char *cname, const char *wname, const char *hname,
                           const AssetImage *img) {
    size_t n = (size_t)img->w * (size_t)img->h * 4u;
    if (buf_printf(b, "#define %s %d\n#define %s %d\n\n", wname, img->w, hname, img->h) != 0)
        return -1;
    if (buf_printf(b, "static const unsigned char %s[%zu] = {\n", cname, n) != 0)
        return -1;
    if (emit_u8_lines(b, img->rgba, n, 20) != 0)
        return -1;
    if (buf_printf(b, "};\n\n") != 0)
        return -1;
    return 0;
}

static int gen_sky(AssetJar *jar, const char *out_dir) {
    AssetImage sun, moon_strip, moon, clouds, end_sky;
    Buf b;
    int cw, ch;

    memset(&sun, 0, sizeof(sun));
    memset(&moon_strip, 0, sizeof(moon_strip));
    memset(&moon, 0, sizeof(moon));
    memset(&clouds, 0, sizeof(clouds));
    memset(&end_sky, 0, sizeof(end_sky));
    memset(&b, 0, sizeof(b));

    if (asset_image_load(jar, "assets/minecraft/textures/environment/sun.png", &sun) != 0)
        goto fail;
    if (asset_image_load(jar, "assets/minecraft/textures/environment/moon_phases.png",
                         &moon_strip) != 0)
        goto fail;
    if (asset_image_load(jar, "assets/minecraft/textures/environment/clouds.png", &clouds) !=
        0)
        goto fail;
    if (asset_image_load(jar, "assets/minecraft/textures/environment/end_sky.png",
                         &end_sky) != 0)
        goto fail;
    cw = moon_strip.w / 4;
    ch = moon_strip.h / 2;
    if (asset_image_crop(&moon_strip, 0, 0, cw, ch, &moon) != 0)
        goto fail;

    if (buf_init(&b) != 0)
        goto fail;
    if (buf_printf(&b,
                   "/* GENERATED by make -C magma assets - DO NOT EDIT.\n"
                   " * Real MC 1.11.2 celestial/atmosphere textures (RGBA, R,G,B,A per texel).\n"
                   " *   sun.png (32x32), moon_phases full-moon cell (32x32), clouds.png (256x256),\n"
                   " *   end_sky.png (128x128 repeating End cube texture).\n"
                   " * Consumed by game/sky.c for the textured sun/moon quads + cloud plane. */\n"
                   "#ifndef MAGMA_SKY_ATLAS_H\n#define MAGMA_SKY_ATLAS_H\n\n") != 0)
        goto fail;
    if (emit_rgba_array(&b, "CR_SUN_RGBA", "CR_SUN_W", "CR_SUN_H", &sun) != 0)
        goto fail;
    if (emit_rgba_array(&b, "CR_MOON_RGBA", "CR_MOON_W", "CR_MOON_H", &moon) != 0)
        goto fail;
    if (emit_rgba_array(&b, "CR_CLOUDS_RGBA", "CR_CLOUDS_W", "CR_CLOUDS_H", &clouds) != 0)
        goto fail;
    if (emit_rgba_array(&b, "CR_END_SKY_RGBA", "CR_END_SKY_W", "CR_END_SKY_H", &end_sky) !=
        0)
        goto fail;
    if (buf_printf(&b, "#endif /* MAGMA_SKY_ATLAS_H */\n") != 0)
        goto fail;
    if (atomic_write(out_dir, "sky_atlas.h", b.buf) != 0)
        goto fail;

    buf_free(&b);
    asset_image_free(&sun);
    asset_image_free(&moon_strip);
    asset_image_free(&moon);
    asset_image_free(&clouds);
    asset_image_free(&end_sky);
    return 0;
fail:
    buf_free(&b);
    asset_image_free(&sun);
    asset_image_free(&moon_strip);
    asset_image_free(&moon);
    asset_image_free(&clouds);
    asset_image_free(&end_sky);
    return -1;
}

static int gen_underwater(AssetJar *jar, const char *out_dir) {
    AssetImage img;
    Buf b;
    size_t n;

    if (asset_image_load(jar, "assets/minecraft/textures/misc/underwater.png", &img) != 0)
        return -1;
    if (buf_init(&b) != 0) {
        asset_image_free(&img);
        return -1;
    }
    n = (size_t)img.w * (size_t)img.h * 4u;
    if (buf_printf(&b,
                   "/* GENERATED by make -C magma assets - DO NOT EDIT.\n"
                   " * MC 1.11.2 textures/misc/underwater.png, RGBA row-major. */\n"
                   "#ifndef MAGMA_ASSETS_UNDERWATER_TEX_H\n"
                   "#define MAGMA_ASSETS_UNDERWATER_TEX_H\n\n"
                   "#define CR_UNDERWATER_TEX_W %d\n#define CR_UNDERWATER_TEX_H %d\n\n"
                   "static const unsigned char CR_UNDERWATER_TEX[] = {\n",
                   img.w, img.h) != 0)
        goto fail;
    if (emit_u8_lines(&b, img.rgba, n, 16) != 0)
        goto fail;
    if (buf_printf(&b, "};\n\n#endif /* MAGMA_ASSETS_UNDERWATER_TEX_H */\n") != 0)
        goto fail;
    if (atomic_write(out_dir, "underwater_tex.h", b.buf) != 0)
        goto fail;
    buf_free(&b);
    asset_image_free(&img);
    return 0;
fail:
    buf_free(&b);
    asset_image_free(&img);
    return -1;
}

/* Minimal animation-mcmeta parse: frametime, frames[], interpolate. */
typedef struct {
    int frametime;
    int interpolate;
    int *frames;
    int n_frames;
    int has_frames;
} AnimMeta;

static void anim_meta_free(AnimMeta *m) {
    free(m->frames);
    m->frames = NULL;
    m->n_frames = 0;
}

static const char *skip_ws(const char *p) {
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
        p++;
    return p;
}

static int parse_anim_mcmeta(const char *json, size_t len, AnimMeta *out) {
    const char *p = json;
    const char *end = json + len;
    const char *anim;
    char *owned;

    memset(out, 0, sizeof(*out));
    out->frametime = 1;
    out->interpolate = 0;
    /* NUL-terminate copy for simpler scan */
    owned = (char *)malloc(len + 1);
    if (!owned)
        return -1;
    memcpy(owned, json, len);
    owned[len] = '\0';
    p = owned;
    end = owned + len;

    anim = strstr(p, "\"animation\"");
    if (!anim) {
        free(owned);
        return -1;
    }
    p = strchr(anim, '{');
    if (!p) {
        free(owned);
        return -1;
    }
    p++;

    while (p < end) {
        p = skip_ws(p);
        if (p >= end || *p == '}')
            break;
        if (*p == ',') {
            p++;
            continue;
        }
        if (*p != '"') {
            free(owned);
            anim_meta_free(out);
            return -1;
        }
        {
            const char *key = p + 1;
            const char *ke = strchr(key, '"');
            size_t klen;
            if (!ke) {
                free(owned);
                anim_meta_free(out);
                return -1;
            }
            klen = (size_t)(ke - key);
            p = skip_ws(ke + 1);
            if (*p != ':') {
                free(owned);
                anim_meta_free(out);
                return -1;
            }
            p = skip_ws(p + 1);
            if (klen == 9 && strncmp(key, "frametime", 9) == 0) {
                char *ep = NULL;
                long v = strtol(p, &ep, 10);
                if (ep == p || v < 1 || v > 100000) {
                    free(owned);
                    anim_meta_free(out);
                    return -1;
                }
                out->frametime = (int)v;
                p = ep;
            } else if (klen == 11 && strncmp(key, "interpolate", 11) == 0) {
                if (strncmp(p, "true", 4) == 0) {
                    out->interpolate = 1;
                    p += 4;
                } else if (strncmp(p, "false", 5) == 0) {
                    out->interpolate = 0;
                    p += 5;
                } else {
                    free(owned);
                    anim_meta_free(out);
                    return -1;
                }
            } else if (klen == 6 && strncmp(key, "frames", 6) == 0) {
                int cap = 16, n = 0;
                int *arr;
                if (*p != '[') {
                    free(owned);
                    anim_meta_free(out);
                    return -1;
                }
                p++;
                arr = (int *)malloc((size_t)cap * sizeof(int));
                if (!arr) {
                    free(owned);
                    anim_meta_free(out);
                    return -1;
                }
                p = skip_ws(p);
                while (*p != ']') {
                    char *ep = NULL;
                    long v;
                    if (*p == ',') {
                        p = skip_ws(p + 1);
                        continue;
                    }
                    v = strtol(p, &ep, 10);
                    if (ep == p) {
                        /* object frames not supported */
                        free(arr);
                        free(owned);
                        anim_meta_free(out);
                        return -1;
                    }
                    if (n >= cap) {
                        int *na;
                        cap *= 2;
                        na = (int *)realloc(arr, (size_t)cap * sizeof(int));
                        if (!na) {
                            free(arr);
                            free(owned);
                            anim_meta_free(out);
                            return -1;
                        }
                        arr = na;
                    }
                    arr[n++] = (int)v;
                    p = skip_ws(ep);
                }
                p++; /* ] */
                free(out->frames);
                out->frames = arr;
                out->n_frames = n;
                out->has_frames = 1;
            } else {
                /* skip unknown value */
                if (*p == '"') {
                    p++;
                    while (*p && *p != '"') {
                        if (*p == '\\' && p[1])
                            p += 2;
                        else
                            p++;
                    }
                    if (*p == '"')
                        p++;
                } else if (*p == '{') {
                    int depth = 1;
                    p++;
                    while (*p && depth) {
                        if (*p == '{')
                            depth++;
                        else if (*p == '}')
                            depth--;
                        p++;
                    }
                } else if (*p == '[') {
                    int depth = 1;
                    p++;
                    while (*p && depth) {
                        if (*p == '[')
                            depth++;
                        else if (*p == ']')
                            depth--;
                        p++;
                    }
                } else {
                    while (*p && *p != ',' && *p != '}')
                        p++;
                }
            }
        }
    }
    free(owned);
    if (out->interpolate)
        return -1;
    return 0;
}

static int gen_animations(AssetJar *jar, const char *out_dir) {
    static const char *SPECS[] = {"water_still",  "water_flow",   "lava_still",
                                  "lava_flow",    "fire_layer_0", "fire_layer_1"};
    Buf b;
    int s;

    if (buf_init(&b) != 0)
        return -1;
    if (buf_printf(&b,
                   "/* GENERATED vanilla block animation frames - DO NOT EDIT. */\n"
                   "/* make -C magma assets */\n"
                   "#ifndef MAGMA_WATER_FRAMES_H\n#define MAGMA_WATER_FRAMES_H\n") != 0)
        goto fail;

    for (s = 0; s < 6; s++) {
        const char *name = SPECS[s];
        char png_path[160], meta_path[176], tag[64];
        unsigned char *meta_data = NULL;
        size_t meta_size = 0;
        AssetImage strip;
        AnimMeta meta;
        int count, frame_size, physical, i;
        int *sequence = NULL;
        int seq_len = 0;

        memset(&strip, 0, sizeof(strip));
        memset(&meta, 0, sizeof(meta));
        upper_copy(name, tag, sizeof(tag));
        if (snprintf(png_path, sizeof(png_path),
                     "assets/minecraft/textures/blocks/%s.png", name) >= (int)sizeof(png_path))
            goto fail;
        if (snprintf(meta_path, sizeof(meta_path), "%s.mcmeta", png_path) >=
            (int)sizeof(meta_path))
            goto fail;
        if (asset_image_load(jar, png_path, &strip) != 0)
            goto fail;
        if (strip.h % strip.w != 0) {
            asset_image_free(&strip);
            goto fail;
        }
        count = strip.h / strip.w;
        if (asset_jar_read(jar, meta_path, &meta_data, &meta_size) != 0) {
            asset_image_free(&strip);
            goto fail;
        }
        if (parse_anim_mcmeta((const char *)meta_data, meta_size, &meta) != 0) {
            free(meta_data);
            asset_image_free(&strip);
            goto fail;
        }
        free(meta_data);
        {
            int frametime = meta.frametime;
            if (meta.has_frames) {
                sequence = meta.frames;
                seq_len = meta.n_frames;
                meta.frames = NULL;
            } else {
                seq_len = count;
                sequence = (int *)malloc((size_t)seq_len * sizeof(int));
                if (!sequence) {
                    anim_meta_free(&meta);
                    asset_image_free(&strip);
                    goto fail;
                }
                for (i = 0; i < seq_len; i++)
                    sequence[i] = i;
            }
            anim_meta_free(&meta);

            if (is_native_flow(name)) {
                frame_size = 32;
                if (strip.w != 32) {
                    free(sequence);
                    asset_image_free(&strip);
                    goto fail;
                }
            } else {
                frame_size = 16;
            }

            if (buf_printf(&b,
                           "#define CR_%s_FRAMES %d\n"
                           "#define CR_%s_FRAMETIME %d\n"
                           "#define CR_%s_SEQUENCE_LEN %d\n"
                           "#define CR_%s_W %d\n"
                           "#define CR_%s_H %d\n",
                           tag, count, tag, frametime, tag, seq_len, tag, frame_size, tag,
                           frame_size) != 0) {
                free(sequence);
                asset_image_free(&strip);
                goto fail;
            }
        }

        if (buf_printf(&b, "static const unsigned char CR_%s_SEQUENCE[%d] = {", tag,
                       seq_len) != 0) {
            free(sequence);
            asset_image_free(&strip);
            goto fail;
        }
        for (i = 0; i < seq_len; i++) {
            if (buf_printf(&b, "%s%d", i ? ", " : "", sequence[i]) != 0) {
                free(sequence);
                asset_image_free(&strip);
                goto fail;
            }
        }
        free(sequence);
        if (buf_printf(&b, "};\n") != 0) {
            asset_image_free(&strip);
            goto fail;
        }
        if (buf_printf(&b,
                       "static const unsigned char CR_%s_RGBA[%d][%d*%d*4] = {\n", tag,
                       count, frame_size, frame_size) != 0) {
            asset_image_free(&strip);
            goto fail;
        }
        for (physical = 0; physical < count; physical++) {
            AssetImage tile, resized;
            memset(&resized, 0, sizeof(resized));
            if (asset_image_crop(&strip, 0, physical * strip.w, strip.w, strip.w, &tile) !=
                0) {
                asset_image_free(&strip);
                goto fail;
            }
            if (strip.w != frame_size) {
                if (asset_image_resize_nearest(&tile, frame_size, frame_size, &resized) !=
                    0) {
                    asset_image_free(&tile);
                    asset_image_free(&strip);
                    goto fail;
                }
                asset_image_free(&tile);
                tile = resized;
            }
            if (buf_printf(&b, "  {\n") != 0) {
                asset_image_free(&tile);
                asset_image_free(&strip);
                goto fail;
            }
            {
                size_t nbytes = (size_t)frame_size * (size_t)frame_size * 4u;
                size_t row;
                /* 32 bytes per line as Python */
                for (row = 0; row < nbytes; row += 32) {
                    size_t chunk = nbytes - row < 32 ? nbytes - row : 32;
                    size_t bi;
                    if (buf_printf(&b, "    ") != 0) {
                        asset_image_free(&tile);
                        asset_image_free(&strip);
                        goto fail;
                    }
                    for (bi = 0; bi < chunk; bi++) {
                        if (buf_printf(&b, "%s%d", bi ? ", " : "",
                                       (int)tile.rgba[row + bi]) != 0) {
                            asset_image_free(&tile);
                            asset_image_free(&strip);
                            goto fail;
                        }
                    }
                    if (buf_printf(&b, ",\n") != 0) {
                        asset_image_free(&tile);
                        asset_image_free(&strip);
                        goto fail;
                    }
                }
            }
            if (buf_printf(&b, "  },\n") != 0) {
                asset_image_free(&tile);
                asset_image_free(&strip);
                goto fail;
            }
            asset_image_free(&tile);
        }
        if (buf_printf(&b, "};\n") != 0) {
            asset_image_free(&strip);
            goto fail;
        }
        asset_image_free(&strip);
    }

    if (buf_printf(&b, "#endif\n") != 0)
        goto fail;
    if (atomic_write(out_dir, "water_frames.h", b.buf) != 0)
        goto fail;
    buf_free(&b);
    return 0;
fail:
    buf_free(&b);
    return -1;
}

int asset_build_world(AssetJar *jar, const char *out_dir, const char *only) {
    if (!jar || !out_dir)
        return -1;
    if (!only) {
        if (gen_atlas(jar, out_dir) != 0)
            return -1;
        if (gen_colormap(jar, out_dir) != 0)
            return -1;
        if (gen_loading(jar, out_dir) != 0)
            return -1;
        if (gen_portal(jar, out_dir) != 0)
            return -1;
        if (gen_sky(jar, out_dir) != 0)
            return -1;
        if (gen_underwater(jar, out_dir) != 0)
            return -1;
        if (gen_animations(jar, out_dir) != 0)
            return -1;
        return 0;
    }
    if (strcmp(only, "atlas") == 0)
        return gen_atlas(jar, out_dir);
    if (strcmp(only, "colormap") == 0)
        return gen_colormap(jar, out_dir);
    if (strcmp(only, "loading") == 0)
        return gen_loading(jar, out_dir);
    if (strcmp(only, "portal") == 0)
        return gen_portal(jar, out_dir);
    if (strcmp(only, "sky") == 0)
        return gen_sky(jar, out_dir);
    if (strcmp(only, "underwater") == 0)
        return gen_underwater(jar, out_dir);
    if (strcmp(only, "animations") == 0)
        return gen_animations(jar, out_dir);
    return -1;
}
