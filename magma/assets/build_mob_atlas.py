#!/usr/bin/env python3
"""Build a deterministic RGBA mob-texture atlas from real MC 1.11.2 entity PNGs.

Run: uv run --no-project --with pillow python assets/build_mob_atlas.py

Packs every needed mob skin at NATIVE resolution (64x64 or 64x32 - no squashing)
into one power-of-2 RGBA atlas via simple shelf packing (sorted tallest-first).
Emits assets/mob_atlas.h with the atlas bytes + a sprite table giving each
skin's atlas rect (x0,y0,x1,y1) AND its native (w,h), so entity_render.c can
compute UVs in skin-texel space exactly like the vanilla ModelBox net.
Also dumps /tmp/magma_mob_atlas.png for eyeballing.

The end-crystal beam uses GL_REPEAT over a narrow 16x256 texture, so it cannot
share atlas UV semantics. The same generated header therefore also carries that
jar image as a standalone, contiguous RGBA array.

Does NOT touch build_atlas.py / atlas_gen.h (block atlas).
"""
import os
import zipfile
import io

from PIL import Image

from mc_jar import find_jar  # noqa: E402
JAR = find_jar()
ENTITY = "assets/minecraft/textures/entity/"
HERE = os.path.dirname(os.path.abspath(__file__))
OUT_H = os.path.join(HERE, "mob_atlas.h")
DUMP_PNG = os.path.join(os.environ.get("TMPDIR", "/tmp"),
                        "magma_mob_atlas.png")
BEAM_MEMBER = ENTITY + "endercrystal/endercrystal_beam.png"

# (symbolic name, jar member relative to ENTITY). Sorted by name for a
# deterministic sprite-index order (the CR_MOB_* defines).
MOB_SPRITES = sorted([
    ("zombie",    "zombie/zombie.png"),      # 64x64
    ("skeleton",  "skeleton/skeleton.png"),  # 64x32
    ("creeper",   "creeper/creeper.png"),    # 64x32
    ("spider",    "spider/spider.png"),      # 64x32
    ("enderman",  "enderman/enderman.png"),  # 64x32
    ("blaze",     "blaze.png"),              # 64x32
    ("sheep",     "sheep/sheep.png"),        # 64x32
    ("sheep_fur", "sheep/sheep_fur.png"),    # 64x32
    ("pig",       "pig/pig.png"),            # 64x32
    ("cow",       "cow/cow.png"),            # 64x32
    ("chicken",   "chicken.png"),            # 64x32
    ("squid",     "squid.png"),              # 64x32
    # skin-variant mobs: same model + texture layout as their base mob,
    # different skin (GmEntityView.skin override in entity_render.c).
    ("pigman",      "zombie_pigman.png"),       # 64x64, zombie layout
    ("husk",        "zombie/husk.png"),         # 64x64, zombie layout
    ("stray",       "skeleton/stray.png"),      # 64x32, skeleton layout
    ("wither_skeleton", "skeleton/wither_skeleton.png"),  # 64x32, skeleton layout
    ("cave_spider", "spider/cave_spider.png"),  # 64x32, spider layout
    ("mooshroom",   "cow/mooshroom.png"),       # 64x32, cow layout
    # own-model mobs (2026-07-13 coverage pass)
    ("witch",       "witch.png"),               # 64x128, ModelWitch
    ("bat",         "bat.png"),                 # 64x64,  ModelBat
    ("llama",       "llama/llama_creamy.png"),  # 128x64, ModelLlama (variant 0)
    ("ghast",       "ghast/ghast.png"),         # 64x32,  ModelGhast
    ("magmacube",   "slime/magmacube.png"),     # 64x32,  ModelMagmaCube
    ("minecart",    "minecart.png"),            # 64x32,  ModelMinecart
    ("arrow",       "projectiles/arrow.png"),   # 32x32,  RenderArrow flat quads
    ("endercrystal", "endercrystal/endercrystal.png"),  # 128x64, ModelEnderCrystal
    ("dragon",       "enderdragon/dragon.png"),          # 256x256, ModelDragon
], key=lambda t: t[0])
# Preserve every existing sprite index; append-only for stable CR_MOB_* ids.
MOB_SPRITES += [("armorstand", "armorstand/wood.png")]
# RenderXPOrb: full 64x64 experience_orb.png (4x4 grid of 16x16 value tiers).
MOB_SPRITES += [("experience_orb", "experience_orb.png")]
# Live roster additions (append only; do not re-sort into MOB_SPRITES).
MOB_SPRITES += [
    ("slime", "slime/slime.png"),           # 64x32 ModelSlime
    ("silverfish", "silverfish.png"),       # 64x32 ModelSilverfish
    ("boat", "boat/boat_oak.png"),          # 64x32 ModelBoat oak
]
# Pixel-path textures (append only). dragon_exploding is the per-texel death
# alpha mask (same 256 UV layout as dragon.png). particles.png is the vanilla
# 128x128 particle sheet (portal / FXLayer 0). explosion.png is the custom
# ParticleExplosionLarge sheet (FXLayer 3, 4x4 frames of 32x32 on 128x128).
MOB_SPRITES += [
    ("dragon_exploding", "enderdragon/dragon_exploding.png"),  # 256x256
    ("particles", "../particle/particles.png"),                # 128x128
    ("explosion", "explosion.png"),                            # 128x128
]
# RenderDragon.renderCrystalBeams healing beam. 16x256, sampled with GL_REPEAT
# on V (the scroll runs the texture off both ends); entity_render.c splits the
# strip at every integer V so the atlas rect is only ever sampled in [0,1).
MOB_SPRITES += [
    ("endercrystal_beam", "endercrystal/endercrystal_beam.png"),  # 16x256
]
# LayerBipedArmor textures used by armor stands. Keep append-only so every
# existing CR_MOB_* index remains stable.
MOB_SPRITES += [
    ("iron_layer_1", "../models/armor/iron_layer_1.png"),
    ("iron_layer_2", "../models/armor/iron_layer_2.png"),
    ("diamond_layer_1", "../models/armor/diamond_layer_1.png"),
    ("diamond_layer_2", "../models/armor/diamond_layer_2.png"),
]
# Generated-village resident skins. Append only: runtime and recorded-entity
# skin ids rely on every earlier CR_MOB_* value remaining stable.
MOB_SPRITES += [
    ("villager", "villager/villager.png"),
    ("villager_farmer", "villager/farmer.png"),
    ("villager_librarian", "villager/librarian.png"),
    ("villager_priest", "villager/priest.png"),
    ("villager_smith", "villager/smith.png"),
    ("villager_butcher", "villager/butcher.png"),
]


def next_pow2(n):
    p = 1
    while p < n:
        p <<= 1
    return p


def shelf_pack(sizes, canvas_w):
    """Shelf packing: items placed left-to-right in shelves; a new shelf opens
    when the current one is full. `sizes` is [(idx, w, h)] sorted tallest-first.
    Returns ({idx: (x, y)}, used_height)."""
    pos = {}
    x = y = shelf_h = 0
    for idx, w, h in sizes:
        if x + w > canvas_w:
            y += shelf_h
            x = shelf_h = 0
        pos[idx] = (x, y)
        x += w
        shelf_h = max(shelf_h, h)
    return pos, y + shelf_h


def main():
    if not os.path.exists(JAR):
        raise SystemExit("jar not found: " + JAR)

    imgs = []
    with zipfile.ZipFile(JAR) as zf:
        for name, member in MOB_SPRITES:
            # members are relative to textures/entity/ unless they start with
            # "assets/" or contain ".." (particle sheet lives next to entity/).
            if member.startswith("assets/"):
                path = member
            elif member.startswith("../"):
                path = "assets/minecraft/textures/" + member[3:]
            else:
                path = ENTITY + member
            data = zf.read(path)
            img = Image.open(io.BytesIO(data)).convert("RGBA")
            imgs.append((name, img))
        beam = Image.open(io.BytesIO(zf.read(BEAM_MEMBER))).convert("RGBA")
    if beam.size != (16, 256):
        raise SystemExit("unexpected end-crystal beam size: %dx%d" % beam.size)

    # shelf-pack at native size onto a pow2 canvas
    # The first 36 sprites predate the armor layers and their packed positions
    # are part of the generated-header stability contract. New append-only
    # sprites pack after that stable prefix within each height.
    stable_prefix = 36
    order = sorted(
        range(len(imgs)),
        key=lambda i: (
            -imgs[i][1].height,
            0 if i < stable_prefix else 1,
            imgs[i][0] if i < stable_prefix else i,
        ),
    )
    sizes = [(i, imgs[i][1].width, imgs[i][1].height) for i in order]
    canvas_w = next_pow2(max(w for _, w, _ in sizes) * 4)  # 4 skins per shelf
    pos, used_h = shelf_pack(sizes, canvas_w)
    canvas_h = next_pow2(used_h)

    atlas = Image.new("RGBA", (canvas_w, canvas_h), (0, 0, 0, 0))
    rects = []  # (name, x0, y0, x1, y1, w, h) in MOB_SPRITES order
    for i, (name, img) in enumerate(imgs):
        x, y = pos[i]
        atlas.paste(img, (x, y))
        rects.append((name, x, y, x + img.width, y + img.height,
                      img.width, img.height))

    atlas.save(DUMP_PNG)

    px = atlas.tobytes()
    assert len(px) == canvas_w * canvas_h * 4

    with open(OUT_H, "w") as f:
        f.write("/* GENERATED by assets/build_mob_atlas.py - DO NOT EDIT.\n")
        f.write(" * Real MC 1.11.2 mob (entity) skins at NATIVE resolution, shelf-packed\n")
        f.write(" * into one RGBA atlas. Each sprite records its atlas rect plus its native\n")
        f.write(" * (w,h) so UVs can be computed in skin-texel space (vanilla ModelBox net).\n")
        f.write(" * Byte order per texel: R,G,B,A (matches CrRgba memory layout). */\n")
        f.write("#ifndef MAGMA_MOB_ATLAS_H\n#define MAGMA_MOB_ATLAS_H\n\n")
        f.write("#define CR_MOB_ATLAS_W %d\n" % canvas_w)
        f.write("#define CR_MOB_ATLAS_H %d\n" % canvas_h)
        f.write("#define CR_MOB_ATLAS_TILE 64\n")   # legacy: dominant skin width
        f.write("#define CR_MOB_SPRITE_COUNT %d\n\n" % len(rects))

        for i, r in enumerate(rects):
            f.write("#define CR_MOB_%s %d\n" % (r[0].upper(), i))
        f.write("\n")

        f.write("typedef struct { const char *name; int x0, y0, x1, y1; int w, h; }"
                " CrMobSprite;\n")
        f.write("static const CrMobSprite CR_MOB_SPRITES[CR_MOB_SPRITE_COUNT] = {\n")
        for name, x0, y0, x1, y1, w, h in rects:
            f.write('    { "%s", %d, %d, %d, %d, %d, %d },\n'
                    % (name, x0, y0, x1, y1, w, h))
        f.write("};\n\n")

        f.write("static const unsigned char CR_MOB_ATLAS_RGBA[%d] = {\n" % len(px))
        for i in range(0, len(px), 16):
            chunk = px[i:i + 16]
            f.write("    " + ",".join("%d" % b for b in chunk) + ",\n")
        f.write("};\n\n")

        beam_px = beam.tobytes()
        f.write("#define CR_ENDERCRYSTAL_BEAM_W %d\n" % beam.width)
        f.write("#define CR_ENDERCRYSTAL_BEAM_H %d\n" % beam.height)
        f.write("static const unsigned char CR_ENDERCRYSTAL_BEAM_RGBA[%d] = {\n"
                % len(beam_px))
        for i in range(0, len(beam_px), 16):
            chunk = beam_px[i:i + 16]
            f.write("    " + ",".join("%d" % b for b in chunk) + ",\n")
        f.write("};\n\n")
        f.write("#endif /* MAGMA_MOB_ATLAS_H */\n")

    print("mob atlas: %dx%d px, %d sprites" % (canvas_w, canvas_h, len(rects)))
    for r in rects:
        print("  %-10s at (%d,%d) native %dx%d" % (r[0], r[1], r[2], r[5], r[6]))
    print("wrote: %s (%d bytes of pixel data)" % (OUT_H, len(px)))
    print("dumped atlas PNG: %s" % DUMP_PNG)


if __name__ == "__main__":
    main()
