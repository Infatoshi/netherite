#!/usr/bin/env python3
"""Extract every runtime block-animation strip into assets/water_frames.h.

The historical filename is retained because bootstrap_assets.sh already invokes
this generator.  Frame times and custom physical-frame sequences come directly
from each PNG's .mcmeta, matching TextureAtlasSprite.updateAnimation.

Run: uv run --no-project --with pillow python assets/build_water_frames.py
"""
import io
import json
import os
import zipfile

from PIL import Image

from mc_jar import find_jar

JAR = find_jar()
HERE = os.path.dirname(os.path.abspath(__file__))
OUT_H = os.path.join(HERE, "water_frames.h")
SPECS = (
    "water_still", "water_flow", "lava_still", "lava_flow",
    "fire_layer_0", "fire_layer_1",
)
NATIVE_FLOW_SPRITES = {"water_flow", "lava_flow"}


def macro(name: str) -> str:
    return name.upper()


def main():
    with zipfile.ZipFile(JAR) as z:
        animations = []
        for name in SPECS:
            base = f"assets/minecraft/textures/blocks/{name}.png"
            im = Image.open(io.BytesIO(z.read(base))).convert("RGBA")
            width, height = im.size
            assert height % width == 0, (name, im.size)
            count = height // width
            meta = json.loads(z.read(base + ".mcmeta"))["animation"]
            # None of these 1.11.2 block sprites requests interpolation.  Keep
            # this explicit so a changed resource cannot silently use hard
            # frame switches where TextureAtlasSprite would blend.
            assert not meta.get("interpolate", False), (name, "interpolation")
            frame_time = int(meta.get("frametime", 1))
            sequence = [int(v) for v in meta.get("frames", range(count))]
            frame_size = width if name in NATIVE_FLOW_SPRITES else 16
            if name in NATIVE_FLOW_SPRITES and width != 32:
                raise SystemExit(f"native flow sprite is not 32px: {name} is {width}")
            frames = []
            for physical in range(count):
                tile = im.crop((0, physical * width, width,
                                (physical + 1) * width))
                if width != frame_size:
                    tile = tile.resize((frame_size, frame_size),
                                       Image.Resampling.NEAREST)
                frames.append(tile.tobytes())
            animations.append((name, frame_time, sequence, frame_size, frames))

    with open(OUT_H, "w") as f:
        f.write("/* GENERATED vanilla block animation frames - DO NOT EDIT. */\n")
        f.write("#ifndef MAGMA_WATER_FRAMES_H\n#define MAGMA_WATER_FRAMES_H\n")
        for name, frame_time, sequence, frame_size, frames in animations:
            tag = macro(name)
            f.write(f"#define CR_{tag}_FRAMES {len(frames)}\n")
            f.write(f"#define CR_{tag}_FRAMETIME {frame_time}\n")
            f.write(f"#define CR_{tag}_SEQUENCE_LEN {len(sequence)}\n")
            f.write(f"#define CR_{tag}_W {frame_size}\n")
            f.write(f"#define CR_{tag}_H {frame_size}\n")
            f.write(f"static const unsigned char CR_{tag}_SEQUENCE"
                    f"[{len(sequence)}] = {{")
            f.write(", ".join(str(v) for v in sequence) + "};\n")
            f.write("static const unsigned char "
                    f"CR_{tag}_RGBA[{len(frames)}]"
                    f"[{frame_size}*{frame_size}*4] = {{\n")
            for tile in frames:
                f.write("  {\n")
                for row in range(len(tile) // 32):
                    vals = tile[row * 32:(row + 1) * 32]
                    f.write("    " + ", ".join(str(b) for b in vals) + ",\n")
                f.write("  },\n")
            f.write("};\n")
        f.write("#endif\n")
    print(f"wrote {OUT_H}")


if __name__ == "__main__":
    main()
