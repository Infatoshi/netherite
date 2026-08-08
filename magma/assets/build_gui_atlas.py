#!/usr/bin/env python3
"""Extract the MC 1.11.2 container-GUI art into a generated C header.

Run: uv run --no-project --with pillow python assets/build_gui_atlas.py

Same zipfile pattern as build_hud_atlas.py. Pulls from the client jar:
  - the 176x166 container panels (inventory / crafting / furnace / brewing)
  - the furnace progress sprites (flame 176,0,14x14 and arrow 176,14,24x17,
    the GuiFurnace drawTexturedModalRect sources)
  - the default font sheet font/ascii.png (128x128, 16x16 grid of 8x8 cells;
    the C font renderer recomputes vanilla FontRenderer.charWidth from it)
  - flat 16x16 item icons for every item id the sim can produce, plus flat
    block-texture tiles for block-items (an honest stand-in: vanilla renders
    block items as mini 3D blocks, we do not)

Emits assets/gui_atlas.h: a sprite table + one flat RGBA array, plus an
(item id -> sprite index) table GUI_ITEM_ICONS. Dumps /tmp/magma_gui_*.png.
"""
import io
import os
import zipfile

from PIL import Image

from mc_jar import find_jar  # noqa: E402
JAR = find_jar()
TEX = "assets/minecraft/textures/"
HERE = os.path.dirname(os.path.abspath(__file__))
OUT_H = os.path.join(HERE, "gui_atlas.h")

# (symbol, source path under textures/, x, y, w, h)
# CHEST_PANEL and LARGE_CHEST_PANEL are assembled from generic_54.png the way
# GuiChest draws them: a row-count-dependent top followed by the common
# player-inventory strip at texture y=126.
SPRITES = [
    ("INV_PANEL",     "gui/container/inventory.png",        0,  0, 176, 166),
    ("TABLE_PANEL",   "gui/container/crafting_table.png",   0,  0, 176, 166),
    ("FURNACE_PANEL", "gui/container/furnace.png",          0,  0, 176, 166),
    ("FURNACE_FLAME", "gui/container/furnace.png",        176,  0,  14,  14),
    ("FURNACE_ARROW", "gui/container/furnace.png",        176, 14,  24,  17),
    ("FONT",          "font/ascii.png",                     0,  0, 128, 128),
]
CHEST_PANEL_SYMBOL = "CHEST_PANEL"
LARGE_CHEST_PANEL_SYMBOL = "LARGE_CHEST_PANEL"
BREWING_SPRITES = [
    ("BREWING_PANEL", "gui/container/brewing_stand.png", 0, 0, 176, 166),
    ("BREWING_PROGRESS", "gui/container/brewing_stand.png", 176, 0, 9, 28),
    ("BREWING_FUEL", "gui/container/brewing_stand.png", 176, 29, 18, 4),
    ("BREWING_BUBBLES", "gui/container/brewing_stand.png", 185, 0, 12, 29),
]
ENCHANTING_SPRITES = [
    ("ENCHANTING_PANEL", "gui/container/enchanting_table.png", 0, 0, 176, 166),
    ("ENCHANTING_OPTION_NORMAL", "gui/container/enchanting_table.png",
     0, 166, 108, 19),
    ("ENCHANTING_OPTION_DISABLED", "gui/container/enchanting_table.png",
     0, 185, 108, 19),
    ("ENCHANTING_OPTION_HOVER", "gui/container/enchanting_table.png",
     0, 204, 108, 19),
    ("ENCHANTING_ICON0", "gui/container/enchanting_table.png", 0, 223, 16, 16),
    ("ENCHANTING_ICON1", "gui/container/enchanting_table.png", 16, 223, 16, 16),
    ("ENCHANTING_ICON2", "gui/container/enchanting_table.png", 32, 223, 16, 16),
    ("ENCHANTING_ICON0_DISABLED", "gui/container/enchanting_table.png",
     0, 239, 16, 16),
    ("ENCHANTING_ICON1_DISABLED", "gui/container/enchanting_table.png",
     16, 239, 16, 16),
    ("ENCHANTING_ICON2_DISABLED", "gui/container/enchanting_table.png",
     32, 239, 16, 16),
    ("ENCHANTING_SGA_FONT", "font/ascii_sga.png", 0, 0, 128, 128),
    ("ENCHANTING_BOOK", "entity/enchanting_table_book.png", 0, 0, 64, 32),
]
ANVIL_SPRITES = [
    ("ANVIL_PANEL", "gui/container/anvil.png", 0, 0, 176, 166),
    ("ANVIL_NAME_ACTIVE", "gui/container/anvil.png", 0, 166, 110, 16),
    ("ANVIL_NAME_INACTIVE", "gui/container/anvil.png", 0, 182, 110, 16),
    ("ANVIL_INVALID", "gui/container/anvil.png", 176, 0, 28, 21),
]
MERCHANT_SPRITES = [
    ("MERCHANT_PANEL", "gui/container/villager.png", 0, 0, 176, 166),
    ("MERCHANT_NEXT_NORMAL", "gui/container/villager.png", 176, 0, 12, 19),
    ("MERCHANT_NEXT_HOVER", "gui/container/villager.png", 188, 0, 12, 19),
    ("MERCHANT_NEXT_DISABLED", "gui/container/villager.png", 200, 0, 12, 19),
    ("MERCHANT_PREV_NORMAL", "gui/container/villager.png", 176, 19, 12, 19),
    ("MERCHANT_PREV_HOVER", "gui/container/villager.png", 188, 19, 12, 19),
    ("MERCHANT_PREV_DISABLED", "gui/container/villager.png", 200, 19, 12, 19),
    ("MERCHANT_DISABLED", "gui/container/villager.png", 212, 0, 28, 21),
]
HORSE_SPRITES = [
    ("HORSE_PANEL", "gui/container/horse.png", 0, 0, 176, 166),
    ("HORSE_CHEST", "gui/container/horse.png", 0, 166, 90, 54),
    ("HORSE_SADDLE", "gui/container/horse.png", 18, 220, 18, 18),
    ("HORSE_ARMOR", "gui/container/horse.png", 0, 220, 18, 18),
    ("HORSE_LLAMA_DECOR", "gui/container/horse.png", 36, 220, 18, 18),
]
SHULKER_SPRITES = [
    ("SHULKER_PANEL", "gui/container/shulker_box.png", 0, 0, 176, 167),
]
BEACON_SPRITES = [
    ("BEACON_PANEL", "gui/container/beacon.png", 0, 0, 230, 219),
    ("BEACON_BUTTON_NORMAL", "gui/container/beacon.png", 0, 219, 22, 22),
    ("BEACON_BUTTON_SELECTED", "gui/container/beacon.png", 22, 219, 22, 22),
    ("BEACON_BUTTON_DISABLED", "gui/container/beacon.png", 44, 219, 22, 22),
    ("BEACON_BUTTON_HOVER", "gui/container/beacon.png", 66, 219, 22, 22),
    ("BEACON_CONFIRM", "gui/container/beacon.png", 90, 220, 18, 18),
    ("BEACON_CANCEL", "gui/container/beacon.png", 112, 220, 18, 18),
    ("BEACON_SPEED", "gui/container/inventory.png", 0, 198, 18, 18),
    ("BEACON_HASTE", "gui/container/inventory.png", 36, 198, 18, 18),
    ("BEACON_RESISTANCE", "gui/container/inventory.png", 108, 216, 18, 18),
    ("BEACON_JUMP", "gui/container/inventory.png", 36, 216, 18, 18),
    ("BEACON_STRENGTH", "gui/container/inventory.png", 72, 198, 18, 18),
    ("BEACON_REGENERATION", "gui/container/inventory.png", 126, 198, 18, 18),
]
STATIC_CONTAINER_SPRITES = [
    ("DISPENSER_PANEL", "gui/container/dispenser.png", 0, 0, 176, 166),
    ("HOPPER_PANEL", "gui/container/hopper.png", 0, 0, 176, 133),
]
ITEM_RENDER_SPRITES = [
    ("POTION_OVERLAY", "items/potion_overlay.png", 0, 0, 16, 16),
]
CARPET_SPRITES = [
    ("CARPET_WHITE", "blocks/wool_colored_white.png"),
    ("CARPET_ORANGE", "blocks/wool_colored_orange.png"),
    ("CARPET_MAGENTA", "blocks/wool_colored_magenta.png"),
    ("CARPET_LIGHT_BLUE", "blocks/wool_colored_light_blue.png"),
    ("CARPET_YELLOW", "blocks/wool_colored_yellow.png"),
    ("CARPET_LIME", "blocks/wool_colored_lime.png"),
    ("CARPET_PINK", "blocks/wool_colored_pink.png"),
    ("CARPET_GRAY", "blocks/wool_colored_gray.png"),
    ("CARPET_SILVER", "blocks/wool_colored_silver.png"),
    ("CARPET_CYAN", "blocks/wool_colored_cyan.png"),
    ("CARPET_PURPLE", "blocks/wool_colored_purple.png"),
    ("CARPET_BLUE", "blocks/wool_colored_blue.png"),
    ("CARPET_BROWN", "blocks/wool_colored_brown.png"),
    ("CARPET_GREEN", "blocks/wool_colored_green.png"),
    ("CARPET_RED", "blocks/wool_colored_red.png"),
    ("CARPET_BLACK", "blocks/wool_colored_black.png"),
]

# item id -> texture (16x16, taken whole). Block ids (<256) use the flat block
# texture; item ids use textures/items. Anything unmapped falls back to the
# HUD pip color in C.
ITEM_ICONS = [
    (1,   "blocks/stone.png"),
    (3,   "blocks/dirt.png"),
    (4,   "blocks/cobblestone.png"),
    (5,   "blocks/planks_oak.png"),
    (12,  "blocks/sand.png"),
    (13,  "blocks/gravel.png"),
    (15,  "blocks/iron_ore.png"),
    (16,  "blocks/coal_ore.png"),
    (17,  "blocks/log_oak.png"),
    (20,  "blocks/glass.png"),
    (35,  "blocks/wool_colored_white.png"),
    (49,  "blocks/obsidian.png"),
    (50,  "blocks/torch_on.png"),
    (58,  "blocks/crafting_table_front.png"),
    (61,  "blocks/furnace_front_off.png"),
    (256, "items/iron_shovel.png"),
    (257, "items/iron_pickaxe.png"),
    (258, "items/iron_axe.png"),
    (259, "items/flint_and_steel.png"),
    (260, "items/apple.png"),
    (261, "items/bow_standby.png"),
    (262, "items/arrow.png"),
    (263, "items/coal.png"),
    (264, "items/diamond.png"),
    (265, "items/iron_ingot.png"),
    (266, "items/gold_ingot.png"),
    (267, "items/iron_sword.png"),
    (268, "items/wood_sword.png"),
    (269, "items/wood_shovel.png"),
    (270, "items/wood_pickaxe.png"),
    (271, "items/wood_axe.png"),
    (272, "items/stone_sword.png"),
    (273, "items/stone_shovel.png"),
    (274, "items/stone_pickaxe.png"),
    (275, "items/stone_axe.png"),
    (276, "items/diamond_sword.png"),
    (277, "items/diamond_shovel.png"),
    (278, "items/diamond_pickaxe.png"),
    (279, "items/diamond_axe.png"),
    (280, "items/stick.png"),
    (287, "items/string.png"),
    (288, "items/feather.png"),
    (289, "items/gunpowder.png"),
    (295, "items/seeds_wheat.png"),
    (296, "items/wheat.png"),
    (297, "items/bread.png"),
    (299, "items/leather_chestplate.png"),
    (300, "items/leather_leggings.png"),
    (302, "items/chainmail_helmet.png"),
    (303, "items/chainmail_chestplate.png"),
    (304, "items/chainmail_leggings.png"),
    (305, "items/chainmail_boots.png"),
    (306, "items/iron_helmet.png"),
    (307, "items/iron_chestplate.png"),
    (311, "items/diamond_chestplate.png"),
    (318, "items/flint.png"),
    (319, "items/porkchop_raw.png"),
    (320, "items/porkchop_cooked.png"),
    (325, "items/bucket_empty.png"),
    (326, "items/bucket_water.png"),
    (327, "items/bucket_lava.png"),
    (331, "items/redstone_dust.png"),
    (329, "items/saddle.png"),
    (334, "items/leather.png"),
    (339, "items/paper.png"),
    (340, "items/book_normal.png"),
    (344, "items/egg.png"),
    (348, "items/glowstone_dust.png"),
    (351, "items/dye_powder_blue.png"),
    (345, "items/compass_00.png"),
    (346, "items/fishing_rod_uncast.png"),
    (349, "items/fish_cod_raw.png"),
    (350, "items/fish_cod_cooked.png"),
    (352, "items/bone.png"),
    (353, "items/sugar.png"),
    (354, "items/cake.png"),
    (355, "items/bed.png"),
    (357, "items/cookie.png"),
    (359, "items/shears.png"),
    (363, "items/beef_raw.png"),
    (364, "items/beef_cooked.png"),
    (365, "items/chicken_raw.png"),
    (366, "items/chicken_cooked.png"),
    (367, "items/rotten_flesh.png"),
    (368, "items/ender_pearl.png"),
    (369, "items/blaze_rod.png"),
    (370, "items/ghast_tear.png"),
    (372, "items/nether_wart.png"),
    (373, "items/potion_bottle_drinkable.png"),
    (374, "items/potion_bottle_empty.png"),
    (375, "items/spider_eye.png"),
    (376, "items/spider_eye_fermented.png"),
    (377, "items/blaze_powder.png"),
    (378, "items/magma_cream.png"),
    (381, "items/ender_eye.png"),
    (382, "items/melon_speckled.png"),
    (384, "items/experience_bottle.png"),
    (387, "items/book_written.png"),
    (388, "items/emerald.png"),
    (391, "items/carrot.png"),
    (392, "items/potato.png"),
    (395, "items/map_empty.png"),
    (396, "items/carrot_golden.png"),
    (400, "items/pumpkin_pie.png"),
    (403, "items/book_enchanted.png"),
    (414, "items/rabbit_foot.png"),
    (417, "items/iron_horse_armor.png"),
    (418, "items/gold_horse_armor.png"),
    (419, "items/diamond_horse_armor.png"),
    (421, "items/name_tag.png"),
    (437, "items/dragon_breath.png"),
    (438, "items/potion_bottle_splash.png"),
    (441, "items/potion_bottle_lingering.png"),
]


def main():
    if not os.path.exists(JAR):
        raise SystemExit("jar not found: " + JAR)

    with zipfile.ZipFile(JAR) as zf:
        def load(path):
            return Image.open(io.BytesIO(zf.read(TEX + path))).convert("RGBA")

        blob = bytearray()
        table = []  # (symbol, w, h, offset)

        def add(sym, img, dump=True):
            off = len(blob)
            blob.extend(img.tobytes())
            table.append((sym, img.width, img.height, off))
            if dump:
                img.save("/tmp/magma_gui_%s.png" % sym.lower())

        for sym, src, x, y, w, h in SPRITES:
            add(sym, load(src).crop((x, y, x + w, y + h)))

        # GuiChest.drawGuiContainerBackgroundLayer composite of generic_54.png
        g54 = load("gui/container/generic_54.png")
        chest = Image.new("RGBA", (176, 167), (0, 0, 0, 0))
        top_h = 3 * 18 + 17  # inventoryRows*18 + 17 for 3-row single chest
        chest.paste(g54.crop((0, 0, 176, top_h)), (0, 0))
        chest.paste(g54.crop((0, 126, 176, 126 + 96)), (0, top_h))
        add(CHEST_PANEL_SYMBOL, chest)
        large_chest = Image.new("RGBA", (176, 221), (0, 0, 0, 0))
        large_top_h = 6 * 18 + 17
        large_chest.paste(g54.crop((0, 0, 176, large_top_h)), (0, 0))
        large_chest.paste(
            g54.crop((0, 126, 176, 126 + 96)), (0, large_top_h))
        add(LARGE_CHEST_PANEL_SYMBOL, large_chest)

        for sym, src, x, y, w, h in BREWING_SPRITES:
            add(sym, load(src).crop((x, y, x + w, y + h)))

        for sym, src, x, y, w, h in ENCHANTING_SPRITES:
            add(sym, load(src).crop((x, y, x + w, y + h)))

        for sym, src, x, y, w, h in ANVIL_SPRITES:
            add(sym, load(src).crop((x, y, x + w, y + h)))

        for sym, src, x, y, w, h in MERCHANT_SPRITES:
            add(sym, load(src).crop((x, y, x + w, y + h)))

        for sym, src, x, y, w, h in HORSE_SPRITES:
            add(sym, load(src).crop((x, y, x + w, y + h)))

        for sym, src, x, y, w, h in SHULKER_SPRITES:
            add(sym, load(src).crop((x, y, x + w, y + h)))

        for sym, src, x, y, w, h in BEACON_SPRITES:
            add(sym, load(src).crop((x, y, x + w, y + h)))

        for sym, src, x, y, w, h in STATIC_CONTAINER_SPRITES:
            add(sym, load(src).crop((x, y, x + w, y + h)))

        for sym, src, x, y, w, h in ITEM_RENDER_SPRITES:
            add(sym, load(src).crop((x, y, x + w, y + h)))

        for sym, src in CARPET_SPRITES:
            add(sym, load(src), dump=False)

        icons = []  # (item_id, sprite_index)
        for item_id, src in ITEM_ICONS:
            img = load(src)
            if img.size != (16, 16):
                raise SystemExit("icon not 16x16: %s is %r" % (src, img.size))
            icons.append((item_id, len(table)))
            add("ITEM_%d" % item_id, img, dump=False)

    assert len(blob) == sum(w * h * 4 for _, w, h, _ in table)
    n_named = (len(SPRITES) + 1 + len(BREWING_SPRITES)
               + len(ENCHANTING_SPRITES) + len(ANVIL_SPRITES)
               + len(MERCHANT_SPRITES) + len(HORSE_SPRITES)
               + len(SHULKER_SPRITES)
               + len(BEACON_SPRITES)
               + len(STATIC_CONTAINER_SPRITES)
               + len(ITEM_RENDER_SPRITES)
               + len(CARPET_SPRITES))

    with open(OUT_H, "w") as f:
        f.write("/* GENERATED by assets/build_gui_atlas.py - DO NOT EDIT.\n")
        f.write(" * Real MC 1.11.2 container GUI art: panels, furnace progress sprites,\n")
        f.write(" * the ascii.png font sheet, and flat 16x16 item icons. Each sprite is\n")
        f.write(" * an RGBA run (R,G,B,A per texel) in one flat array. */\n")
        f.write("#ifndef MAGMA_GUI_ATLAS_H\n#define MAGMA_GUI_ATLAS_H\n\n")
        f.write("#define GUI_SPRITE_COUNT %d\n" % len(table))
        f.write("#define GUI_ITEM_ICON_COUNT %d\n\n" % len(icons))
        for i, (sym, _, _, _) in enumerate(table[:n_named]):
            f.write("#define GUI_%s %d\n" % (sym, i))
        f.write("\n")
        f.write("typedef struct { const char *name; int w, h, off; } GuiSprite;\n")
        f.write("static const GuiSprite GUI_SPRITES[GUI_SPRITE_COUNT] = {\n")
        for sym, w, h, off in table:
            f.write('    { "%s", %d, %d, %d },\n' % (sym, w, h, off))
        f.write("};\n\n")
        f.write("/* item id -> GUI_SPRITES index, ascending by item id */\n")
        f.write("typedef struct { int item, sprite; } GuiItemIcon;\n")
        f.write("static const GuiItemIcon GUI_ITEM_ICONS[GUI_ITEM_ICON_COUNT] = {\n")
        for item_id, idx in sorted(icons):
            f.write("    { %d, %d },\n" % (item_id, idx))
        f.write("};\n\n")
        f.write("static const unsigned char GUI_RGBA[%d] = {\n" % len(blob))
        for i in range(0, len(blob), 16):
            f.write("    " + ",".join("%d" % b for b in blob[i:i + 16]) + ",\n")
        f.write("};\n\n")
        f.write("#endif /* MAGMA_GUI_ATLAS_H */\n")

    print("wrote %s: %d sprites (%d item icons), %d bytes pixel data"
          % (OUT_H, len(table), len(icons), len(blob)))


if __name__ == "__main__":
    main()
