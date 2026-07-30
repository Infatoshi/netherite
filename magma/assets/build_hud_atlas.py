#!/usr/bin/env python3
"""Extract the MC 1.11.2 survival HUD gui sprites into a generated C header.

Run: uv run --no-project --with pillow python assets/build_hud_atlas.py

Mirrors build_atlas.py's zipfile approach: pulls widgets.png / icons.png from the
client jar and slices out the exact sub-rects the vanilla HUD uses (hotbar strip,
selection box, heart / haunch icons, XP bar, crosshair). Each sprite is emitted as
its own RGBA run inside one flat byte array, indexed by a small sprite table, into
assets/hud_atlas.h. Also dumps /tmp/magma_hud_*.png for eyeballing.

Sprite coords are the well-known GuiIngame constants for 1.11.2:
  widgets.png : hotbar (0,0,182x22), selection (0,22,24x24)
  icons.png   : crosshair (0,0,15x15);
                heart bg (16,0), flashing bg (25,0),
                      full (52,0), half (61,0),
                      flashing full (70,0), half (79,0)      each 9x9
                poison full/half (88/97,0), flash (106/115,0)
                wither full/half (124/133,0), flash (142/151,0)
                absorption full/half (160/169,0)              each 9x9
                haunch bg (16,27) full (52,27) half (61,27)  each 9x9
                air full (16,18) partial (25,18)              each 9x9
                xp bar empty (0,64,182x5) full (0,69,182x5)
"""
import io
import os
import zipfile

from PIL import Image

from mc_jar import find_jar  # noqa: E402
JAR = find_jar()
GUI = "assets/minecraft/textures/gui/"
HERE = os.path.dirname(os.path.abspath(__file__))
OUT_H = os.path.join(HERE, "hud_atlas.h")

# (symbol, source_png, x, y, w, h)
SPRITES = [
    ("HOTBAR",      "widgets.png",   0,  0, 182, 22),
    ("SELECT",      "widgets.png",   0, 22,  24, 24),
    ("CROSSHAIR",   "icons.png",     0,  0,  15, 15),
    ("HEART_BG",    "icons.png",    16,  0,   9,  9),
    ("HEART_BG_FLASH", "icons.png", 25,  0,   9,  9),
    ("HEART_FULL",  "icons.png",    52,  0,   9,  9),
    ("HEART_HALF",  "icons.png",    61,  0,   9,  9),
    ("HEART_FLASH_FULL", "icons.png", 70, 0,  9,  9),
    ("HEART_FLASH_HALF", "icons.png", 79, 0,  9,  9),
    ("HEART_POISON_FULL", "icons.png", 88, 0,  9,  9),
    ("HEART_POISON_HALF", "icons.png", 97, 0,  9,  9),
    ("HEART_POISON_FLASH_FULL", "icons.png", 106, 0, 9, 9),
    ("HEART_POISON_FLASH_HALF", "icons.png", 115, 0, 9, 9),
    ("HEART_WITHER_FULL", "icons.png", 124, 0, 9, 9),
    ("HEART_WITHER_HALF", "icons.png", 133, 0, 9, 9),
    ("HEART_WITHER_FLASH_FULL", "icons.png", 142, 0, 9, 9),
    ("HEART_WITHER_FLASH_HALF", "icons.png", 151, 0, 9, 9),
    # GuiIngame.renderPlayerStats absorption: k5+144 full / k5+153 half (k5=16).
    ("HEART_ABSORB_FULL", "icons.png", 160, 0, 9, 9),
    ("HEART_ABSORB_HALF", "icons.png", 169, 0, 9, 9),
    ("HUNGER_BG",   "icons.png",    16, 27,   9,  9),
    ("HUNGER_FULL", "icons.png",    52, 27,   9,  9),
    ("HUNGER_HALF", "icons.png",    61, 27,   9,  9),
    # GuiIngameForge.renderFood under MobEffects.HUNGER: bg index 13, icon+36.
    ("HUNGER_POISON_BG",   "icons.png", 133, 27,  9,  9),
    ("HUNGER_POISON_FULL", "icons.png",  88, 27,  9,  9),
    ("HUNGER_POISON_HALF", "icons.png",  97, 27,  9,  9),
    # GuiIngame.renderPlayerStats armor row: empty / half / full at y=9.
    ("ARMOR_EMPTY", "icons.png",    16,  9,   9,  9),
    ("ARMOR_HALF",  "icons.png",    25,  9,   9,  9),
    ("ARMOR_FULL",  "icons.png",    34,  9,   9,  9),
    ("AIR_FULL",     "icons.png",    16, 18,   9,  9),
    ("AIR_PARTIAL",  "icons.png",    25, 18,   9,  9),
    ("XP_EMPTY",    "icons.png",     0, 64, 182,  5),
    ("XP_FULL",     "icons.png",     0, 69, 182,  5),
    # boss bar (bars.png row 0 = PINK, the ender dragon color): bg + progress
    ("BOSS_PINK_BG",   "bars.png",   0,  0, 182,  5),
    ("BOSS_PINK_FULL", "bars.png",   0,  5, 182,  5),
    # GuiIngame.renderPotionEffects: normal background and the 8x3 icon grid.
    ("POTION_BG", "container/inventory.png", 141, 166, 24, 24),
    ("POTION_ICONS", "container/inventory.png", 0, 198, 144, 54),
    # GuiButton (widgets.png): disabled / enabled / hover 200x20 strips.
    # V-offsets 46 + state*20 with state 0/1/2 (GuiButton.getHoverState).
    ("BUTTON_DISABLED", "widgets.png", 0, 46, 200, 20),
    ("BUTTON_ENABLED",  "widgets.png", 0, 66, 200, 20),
    ("BUTTON_HOVER",    "widgets.png", 0, 86, 200, 20),
]


def main():
    if not os.path.exists(JAR):
        raise SystemExit("jar not found: " + JAR)

    with zipfile.ZipFile(JAR) as zf:
        srcs = {}
        for fn in ("widgets.png", "icons.png", "bars.png",
                   "container/inventory.png"):
            srcs[fn] = Image.open(io.BytesIO(zf.read(GUI + fn))).convert("RGBA")

    blob = bytearray()
    table = []  # (symbol, w, h, offset)
    for sym, src, x, y, w, h in SPRITES:
        crop = srcs[src].crop((x, y, x + w, y + h))
        off = len(blob)
        blob += crop.tobytes()
        table.append((sym, w, h, off))
        crop.save("/tmp/magma_hud_%s.png" % sym.lower())

    assert len(blob) == sum(w * h * 4 for _, w, h, _ in table)

    with open(OUT_H, "w") as f:
        f.write("/* GENERATED by assets/build_hud_atlas.py - DO NOT EDIT.\n")
        f.write(" * Real MC 1.11.2 survival HUD gui sprites (widgets.png / icons.png),\n")
        f.write(" * each stored as an RGBA run (R,G,B,A per texel) in one flat array. */\n")
        f.write("#ifndef MAGMA_HUD_ATLAS_H\n#define MAGMA_HUD_ATLAS_H\n\n")
        f.write("#define HUD_SPRITE_COUNT %d\n\n" % len(table))
        for i, (sym, _, _, _) in enumerate(table):
            f.write("#define HUD_%s %d\n" % (sym, i))
        f.write("\n")
        f.write("typedef struct { const char *name; int w, h, off; } HudSprite;\n")
        f.write("static const HudSprite HUD_SPRITES[HUD_SPRITE_COUNT] = {\n")
        for sym, w, h, off in table:
            f.write('    { "%s", %d, %d, %d },\n' % (sym, w, h, off))
        f.write("};\n\n")
        f.write("static const unsigned char HUD_RGBA[%d] = {\n" % len(blob))
        for i in range(0, len(blob), 16):
            chunk = blob[i:i + 16]
            f.write("    " + ",".join("%d" % b for b in chunk) + ",\n")
        f.write("};\n\n")
        f.write("#endif /* MAGMA_HUD_ATLAS_H */\n")

    print("wrote %s: %d sprites, %d bytes pixel data" % (OUT_H, len(table), len(blob)))


if __name__ == "__main__":
    main()
