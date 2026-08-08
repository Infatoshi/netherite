#!/usr/bin/env python3
"""Build a deterministic RGBA texture atlas from real MC 1.11.2 block PNGs.

Run: uv run --no-project --with pillow python assets/build_atlas.py

Unzips the needed block textures from the client jar, stitches them into one
square power-of-two atlas, and emits a generated C header assets/atlas_gen.h
with the atlas bytes + a sorted sprite rect table. Most sprites are 16x16;
water_flow and lava_flow retain their native 32x32 frames because vanilla's
BlockFluidRenderer addresses their full-resolution TextureAtlasSprites.
Animated textures contribute only their first frame.
Also dumps /tmp/magma_atlas.png for eyeballing.
"""
import io
import os
import tempfile
import zipfile

from PIL import Image

from mc_jar import find_jar  # noqa: E402
JAR = find_jar()
BLOCKS = "assets/minecraft/textures/blocks/"
TILE = 16
HERE = os.path.dirname(os.path.abspath(__file__))
OUT_H = os.path.join(HERE, "atlas_gen.h")
DUMP_PNG = os.path.join(os.environ.get("TMPDIR", tempfile.gettempdir()),
                        "magma_atlas.png")

# Every unique block texture referenced by the CB_*/PB_* worldgen set.
SPRITE_NAMES = sorted([
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
    "hardened_clay_stained_orange",
    "hardened_clay_stained_blue",
    "ice",
    "mycelium_side",
    "mycelium_top",
    "red_sandstone_bottom",
    "red_sandstone_normal",
    "red_sandstone_top",
    "sand",
    "sandstone_bottom",
    "sandstone_carved",
    "sandstone_normal",
    "sandstone_smooth",
    "sandstone_top",
    "snow",
    "stone",
    "water_flow",
    "water_still",
    "waterlily",
    # trees / foliage
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
    # cross plants
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
    # special / ores / structures
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
    # crafting table (vanilla models/block/crafting_table.json)
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
    # nether / end / portal (E2E dim fidelity)
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
    # block-breaking crack overlay (RenderGlobal drawBlockDamageTexture)
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
])
# Deduplicate while preserving sort order (list may mention a name twice).
SPRITE_NAMES = sorted(set(SPRITE_NAMES))
# Append new mechanics-pass sprites so existing generated indices remain stable.
SPRITE_NAMES += ["ice_packed", "slime", "web"]
# TileEntityEndPortalRenderer binds textures/entity/end_portal.png (256x256);
# pack a 16x16 NEAREST downsample so the chunk mesh can tint multi-layer
# UP-face quads with the same starfield source as the Java TESR.
SPRITE_NAMES += ["end_portal"]
# Stone-slab variants. Append so every established atlas index/rect remains
# stable; the slab state bridge selects these by legacy metadata.
SPRITE_NAMES += [
    "brick",
    "quartz_block_bottom",
    "quartz_block_side",
    "quartz_block_top",
    "stone_slab_side",
    "stone_slab_top",
    "stonebrick",
]
# BlockPane uses the glass texture for its faces and a separate two-pixel edge
# texture for the post and connected-arm caps.
SPRITE_NAMES += ["glass_pane_top"]
# BlockTrapDoor's wooden model uses the legacy blocks/trapdoor texture.
SPRITE_NAMES += ["trapdoor"]
# BlockLadder's two-sided cutout wall plane.
SPRITE_NAMES += ["ladder"]
# End City palette. Appended so established atlas indices remain stable.
SPRITE_NAMES += [
    "end_bricks",
    "end_rod",
    "glass_magenta",
    "purpur_block",
    "purpur_pillar",
    "purpur_pillar_top",
    "chorus_plant",
    "chorus_flower",
    "chorus_flower_dead",
]
SPECIAL_SPRITE_PATHS = {
    "end_portal": "assets/minecraft/textures/entity/end_portal.png",
}
NATIVE_FLOW_SPRITES = ("water_flow", "lava_flow")


def load_sprite(zf, tmpdir, name):
    """Extract one PNG and return its first RGBA animation frame."""
    member = SPECIAL_SPRITE_PATHS.get(name, BLOCKS + name + ".png")
    if name in SPECIAL_SPRITE_PATHS:
        img = Image.open(io.BytesIO(zf.read(member))).convert("RGBA")
    else:
        zf.extract(member, tmpdir)
        img = Image.open(os.path.join(tmpdir, member)).convert("RGBA")
    w, h = img.size
    # Animated textures are stored as a vertical strip w x (w*frames): take frame 0.
    if h > w and h % w == 0:
        img = img.crop((0, 0, w, w))
        w, h = img.size
    if name in NATIVE_FLOW_SPRITES:
        if (w, h) != (32, 32):
            raise SystemExit("native flow sprite is not 32x32: %s is %r" %
                             (name, (w, h)))
    elif (w, h) != (TILE, TILE):
        img = img.resize((TILE, TILE), Image.NEAREST)
    return img


def main():
    if not os.path.exists(JAR):
        raise SystemExit("jar not found: " + JAR)

    n = len(SPRITE_NAMES)
    # Square power-of-two grid of 16px tiles that fits all sprites.
    grid = 1
    while grid * grid < n:
        grid *= 2
    grid = max(grid, 1)
    # grid must be power-of-two tiles per side; ensure atlas dim is pow2.
    side_tiles = 1
    while side_tiles * side_tiles < n:
        side_tiles <<= 1
    atlas_dim = side_tiles * TILE  # e.g. 8 tiles -> but we want pow2 px too

    # side_tiles is already a power of two, so atlas_dim = side_tiles*16 is pow2.
    atlas = Image.new("RGBA", (atlas_dim, atlas_dim), (0, 0, 0, 0))
    rects = []  # (name, x0, y0, x1, y1)

    with tempfile.TemporaryDirectory() as tmpdir:
        with zipfile.ZipFile(JAR) as zf:
            for i, name in enumerate(SPRITE_NAMES):
                spr = load_sprite(zf, tmpdir, name)
                if name in NATIVE_FLOW_SPRITES:
                    # Keep stable sprite indices while parking the two native
                    # 32x32 frames in an unused atlas row. Their historical
                    # 16x16 slots remain empty, so every other sprite keeps
                    # the same coordinates.
                    tx = NATIVE_FLOW_SPRITES.index(name) * 32
                    ty = atlas_dim - 32
                else:
                    tx = (i % side_tiles) * TILE
                    ty = (i // side_tiles) * TILE
                    if ty + TILE > atlas_dim - 32:
                        raise SystemExit("sprite grid overlaps native flow row")
                atlas.paste(spr, (tx, ty))
                rects.append((name, tx, ty, tx + spr.width, ty + spr.height))

    atlas.save(DUMP_PNG)

    px = atlas.tobytes()  # RGBA, row-major, len = atlas_dim*atlas_dim*4
    assert len(px) == atlas_dim * atlas_dim * 4

    with open(OUT_H, "w") as f:
        f.write("/* GENERATED by assets/build_atlas.py - DO NOT EDIT.\n")
        f.write(" * Real MC 1.11.2 block textures stitched into one RGBA atlas.\n")
        f.write(" * Byte order per texel: R,G,B,A (matches CrRgba memory layout). */\n")
        f.write("#ifndef MAGMA_ATLAS_GEN_H\n#define MAGMA_ATLAS_GEN_H\n\n")
        f.write("#define CR_ATLAS_W %d\n" % atlas_dim)
        f.write("#define CR_ATLAS_H %d\n" % atlas_dim)
        f.write("#define CR_ATLAS_TILE %d\n" % TILE)
        f.write("#define CR_ATLAS_SPRITE_COUNT %d\n\n" % n)

        # Symbolic sprite indices so blockmodels.c can reference by name.
        for i, (name, _, _, _, _) in enumerate(rects):
            f.write("#define CR_SPRITE_%s %d\n" % (name.upper(), i))
        f.write("\n")

        f.write("typedef struct { const char *name; int x0, y0, x1, y1; } CrAtlasSprite;\n")
        f.write("static const CrAtlasSprite CR_ATLAS_SPRITES[CR_ATLAS_SPRITE_COUNT] = {\n")
        for name, x0, y0, x1, y1 in rects:
            f.write('    { "%s", %d, %d, %d, %d },\n' % (name, x0, y0, x1, y1))
        f.write("};\n\n")

        f.write("static const unsigned char CR_ATLAS_RGBA[%d] = {\n" % len(px))
        for i in range(0, len(px), 16):
            chunk = px[i:i + 16]
            f.write("    " + ",".join("%d" % b for b in chunk) + ",\n")
        f.write("};\n\n")
        f.write("#endif /* MAGMA_ATLAS_GEN_H */\n")

    print("atlas: %dx%d px, %d sprites, %d tiles/side" %
          (atlas_dim, atlas_dim, n, side_tiles))
    print("wrote: %s (%d bytes of pixel data)" % (OUT_H, len(px)))
    print("dumped atlas PNG: %s" % DUMP_PNG)


if __name__ == "__main__":
    main()
