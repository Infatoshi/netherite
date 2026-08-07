#!/usr/bin/env python3
"""Build a deterministic RGBA item-sprite atlas from real MC 1.11.2 item PNGs.

Run: uv run --no-project --with pillow python assets/build_item_atlas.py

Packs the dropped-item sprites the game can spawn (harvest_drop / plant_drop /
route items) at NATIVE 16x16 resolution into one power-of-2 RGBA atlas on a
simple grid. Emits assets/item_atlas.h with the atlas bytes plus a sprite table
keyed by the vanilla 1.11.2 ITEM ID (sorted ascending), so game/item_render.c
can look a stack's sprite up directly. Also dumps /tmp/magma_item_atlas.png.

Modeled on assets/build_mob_atlas.py. Does NOT touch the block or mob atlas.
"""
import io
import os
import zipfile

from PIL import Image

from mc_jar import find_jar  # noqa: E402
JAR = find_jar()
ITEMS = "assets/minecraft/textures/items/"
HERE = os.path.dirname(os.path.abspath(__file__))
OUT_H = os.path.join(HERE, "item_atlas.h")
DUMP_PNG = "/tmp/magma_item_atlas.png"
TILE = 16

# (vanilla item id, jar member relative to ITEMS). Sorted by id below for a
# deterministic table order (lookup by id in item_render.c). Synthetic id 9003
# is the dedicated RenderDragonFireball texture, kept here because both
# fireball renderers share the camera-facing item-atlas pass.
ITEM_SPRITES = [
    (259, "flint_and_steel.png"),
    (260, "apple.png"),
    (263, "coal.png"),
    (264, "diamond.png"),
    (265, "iron_ingot.png"),
    (266, "gold_ingot.png"),
    (267, "iron_sword.png"),
    (268, "wood_sword.png"),
    (269, "wood_shovel.png"),
    (270, "wood_pickaxe.png"),
    (272, "stone_sword.png"),
    (274, "stone_pickaxe.png"),
    (280, "stick.png"),
    (287, "string.png"),
    (288, "feather.png"),
    (289, "gunpowder.png"),
    (295, "seeds_wheat.png"),
    (296, "wheat.png"),
    (297, "bread.png"),  # hand_eat_mid viewmodel (was fallback iron_ingot)
    (318, "flint.png"),
    (319, "porkchop_raw.png"),
    (320, "porkchop_cooked.png"),
    (331, "redstone_dust.png"),
    (334, "leather.png"),
    (338, "reeds.png"),
    (352, "bone.png"),
    (363, "beef_raw.png"),
    (364, "beef_cooked.png"),
    (368, "ender_pearl.png"),
    (369, "blaze_rod.png"),
    (377, "blaze_powder.png"),
    (381, "ender_eye.png"),
    # RenderFireball gets this through the fire_charge item model. In 1.11.2
    # the model's layer0 is textures/items/fireball.png.
    (385, "fireball.png"),
    # held tools (progression segments: mine/build/crystal/dragon). Without a
    # sprite the first-person hand falls back to iron_ingot (mine tape 120328Z
    # rendered a giant gray ingot where the oracle held an iron axe).
    (256, "iron_shovel.png"),
    (257, "iron_pickaxe.png"),
    (258, "iron_axe.png"),
    (262, "arrow.png"),
    (271, "wood_axe.png"),
    (273, "stone_shovel.png"),
    (275, "stone_axe.png"),
    (276, "diamond_sword.png"),
    (277, "diamond_shovel.png"),
    (278, "diamond_pickaxe.png"),
    (279, "diamond_axe.png"),
    # mob held items (LayerHeldItem: pigman gold sword, skeleton/stray bow)
    (283, "gold_sword.png"),
    (261, "bow_standby.png"),
    # bow draw stages (item/bow.json overrides; synthetic ids, hand.c only:
    # pulling_0 while drawing, _1 at pull>=0.65, _2 at pull>=0.9)
    (9000, "bow_pulling_0.png"),
    (9001, "bow_pulling_1.png"),
    (9002, "bow_pulling_2.png"),
    (9003, "dragon_fireball.png"),
    # RenderFish samples particle atlas cell (1,2), UV 1/16..2/16 by
    # 2/16..3/16. Crop that exact 16x16 cell into the shared billboard atlas.
    (9004, "fishing_bobber.png"),
    # shield (442): ModelShield uses entity/shield_base_nopattern (64x64);
    # inventory GUI also paints a downscaled face. Hand path samples the atlas.
    (442, "empty_armor_slot_shield.png"),  # placeholder path; SPECIAL_PATHS overrides
    (443, "elytra.png"),
]
ITEM_SPRITES.sort(key=lambda t: t[0])

SPECIAL_PATHS = {
    9003: "assets/minecraft/textures/entity/enderdragon/dragon_fireball.png",
    9004: "assets/minecraft/textures/particle/particles.png",
    442: "assets/minecraft/textures/entity/shield_base_nopattern.png",
}

SPECIAL_CROPS = {
    9004: (16, 32, 32, 48),
}


def next_pow2(n):
    p = 1
    while p < n:
        p <<= 1
    return p


def main():
    if not os.path.exists(JAR):
        raise SystemExit("jar not found: " + JAR)

    imgs = []
    with zipfile.ZipFile(JAR) as zf:
        for item_id, member in ITEM_SPRITES:
            data = zf.read(SPECIAL_PATHS.get(item_id, ITEMS + member))
            img = Image.open(io.BytesIO(data)).convert("RGBA")
            if item_id in SPECIAL_CROPS:
                img = img.crop(SPECIAL_CROPS[item_id])
            name = member[:-4] if member.endswith(".png") else member
            # ModelShield samples entity/shield_base_nopattern at native 64x64
            # (box-net UVs in texels). Keep full res; do not downscale to 16.
            # Other non-16 entity sprites (dragon fireball) still fit a tile.
            if img.size != (TILE, TILE) and item_id != 442:
                img = img.resize((TILE, TILE), Image.NEAREST)
            imgs.append((item_id, name, img))

    # Shelf-pack native sizes (mostly 16x16; shield is 64x64) onto a pow2 canvas.
    # Sort by height desc so the tall shield lands first and 16x16 fill shelves.
    order = sorted(range(len(imgs)), key=lambda i: (-imgs[i][2].size[1], imgs[i][0]))
    shelves = []  # list of [y, height, x_cursor]
    placed = [None] * len(imgs)  # (x0,y0,x1,y1)
    canvas_w_needed = 0
    canvas_h_needed = 0
    # Prefer ~8 tiles wide for the 16x16 majority (~128 px).
    shelf_width = max(8 * TILE, max(im.size[0] for _, _, im in imgs))
    for i in order:
        item_id, name, img = imgs[i]
        w, h = img.size
        best = None
        for s in shelves:
            if s[1] >= h and s[2] + w <= shelf_width:
                best = s
                break
        if best is None:
            y = canvas_h_needed
            best = [y, h, 0]
            shelves.append(best)
            canvas_h_needed = y + h
        x = best[2]
        best[2] = x + w
        if best[2] > canvas_w_needed:
            canvas_w_needed = best[2]
        placed[i] = (x, best[0], x + w, best[0] + h)

    canvas_w = next_pow2(max(canvas_w_needed, shelf_width))
    canvas_h = next_pow2(canvas_h_needed)
    atlas = Image.new("RGBA", (canvas_w, canvas_h), (0, 0, 0, 0))
    rects = []  # (id, name, x0, y0, x1, y1) sorted by item id
    for i, (item_id, name, img) in enumerate(imgs):
        x0, y0, x1, y1 = placed[i]
        atlas.paste(img, (x0, y0))
        rects.append((item_id, name, x0, y0, x1, y1))
    rects.sort(key=lambda t: t[0])

    atlas.save(DUMP_PNG)

    px = atlas.tobytes()
    assert len(px) == canvas_w * canvas_h * 4

    with open(OUT_H, "w") as f:
        f.write("/* GENERATED by assets/build_item_atlas.py - DO NOT EDIT.\n")
        f.write(" * Real MC 1.11.2 dropped-item sprites (16x16 native) grid-packed into one\n")
        f.write(" * RGBA atlas. CR_ITEM_SPRITES is sorted ascending by vanilla item id.\n")
        f.write(" * Byte order per texel: R,G,B,A (matches CrRgba memory layout). */\n")
        f.write("#ifndef MAGMA_ITEM_ATLAS_H\n#define MAGMA_ITEM_ATLAS_H\n\n")
        f.write("#define CR_ITEM_ATLAS_W %d\n" % canvas_w)
        f.write("#define CR_ITEM_ATLAS_H %d\n" % canvas_h)
        f.write("#define CR_ITEM_ATLAS_TILE %d\n" % TILE)
        f.write("#define CR_ITEM_SPRITE_COUNT %d\n\n" % len(rects))

        f.write("typedef struct { int id; const char *name; int x0, y0, x1, y1; }"
                " CrItemSprite;\n")
        f.write("static const CrItemSprite CR_ITEM_SPRITES[CR_ITEM_SPRITE_COUNT] = {\n")
        for item_id, name, x0, y0, x1, y1 in rects:
            f.write('    { %d, "%s", %d, %d, %d, %d },\n'
                    % (item_id, name, x0, y0, x1, y1))
        f.write("};\n\n")

        f.write("static const unsigned char CR_ITEM_ATLAS_RGBA[%d] = {\n" % len(px))
        for i in range(0, len(px), 16):
            chunk = px[i:i + 16]
            f.write("    " + ",".join("%d" % b for b in chunk) + ",\n")
        f.write("};\n\n")
        f.write("#endif /* MAGMA_ITEM_ATLAS_H */\n")

    print("item atlas: %dx%d px, %d sprites" % (canvas_w, canvas_h, len(rects)))
    for r in rects:
        print("  %4d %-16s at (%d,%d)" % (r[0], r[1], r[2], r[3]))
    print("wrote: %s (%d bytes of pixel data)" % (OUT_H, len(px)))
    print("dumped atlas PNG: %s" % DUMP_PNG)


if __name__ == "__main__":
    main()
