#!/usr/bin/env python3
"""Extract the 32 water_still animation frames into assets/water_frames.h
(CR_WATER_STILL_RGBA, advanced by total_time / CR_WATER_STILL_FRAMETIME in
blockmodels.c, matching TextureAtlasSprite).

Run: uv run --no-project --with pillow python assets/build_water_frames.py
"""
import io
import os
import zipfile

from PIL import Image

from mc_jar import find_jar

JAR = find_jar()
HERE = os.path.dirname(os.path.abspath(__file__))
OUT_H = os.path.join(HERE, "water_frames.h")
TILE = 16
FRAMES = 32
FRAMETIME = 2


def main():
    with zipfile.ZipFile(JAR) as z:
        im = Image.open(io.BytesIO(z.read(
            "assets/minecraft/textures/blocks/water_still.png"))
        ).convert("RGBA")
    assert im.size == (TILE, TILE * FRAMES), im.size
    with open(OUT_H, "w") as f:
        f.write("/* GENERATED water_still animation frames - DO NOT EDIT. */\n")
        f.write("#ifndef MAGMA_WATER_FRAMES_H\n#define MAGMA_WATER_FRAMES_H\n")
        f.write(f"#define CR_WATER_STILL_FRAMES {FRAMES}\n")
        f.write(f"#define CR_WATER_STILL_FRAMETIME {FRAMETIME}\n")
        f.write("static const unsigned char "
                f"CR_WATER_STILL_RGBA[{FRAMES}][{TILE}*{TILE}*4] = {{\n")
        for fr in range(FRAMES):
            tile = im.crop((0, fr * TILE, TILE, (fr + 1) * TILE)).tobytes()
            f.write("  {\n")
            for row in range(len(tile) // 32):   # 32 bytes (8 px) per line
                vals = tile[row * 32:(row + 1) * 32]
                f.write("    " + ", ".join(str(b) for b in vals) + ",\n")
            f.write("  },\n")
        f.write("};\n#endif\n")
    print(f"wrote {OUT_H}")


if __name__ == "__main__":
    main()
