#!/usr/bin/env python3
"""check_mob_atlas.py - verify assets/mob_atlas.h against the REAL MC jar.

Golden source: minecraft-1.11.2.jar assets/minecraft/textures/entity/*.png
(never a self-captured golden). For every CR_MOB_* sprite in mob_atlas.h:
  - the jar texture it names still exists,
  - its native (w,h) matches the jar PNG,
  - every atlas texel inside its rect is byte-identical to the jar pixels.
Also asserts the type->skin routing contract: every skin-variant entity the
tape recorder can emit (pigman/husk/stray/cave spider/mooshroom) has its own
sprite in the atlas (guards a rebuilt atlas that silently drops one).

Run: uv run --no-project --with pillow python tests/check_mob_atlas.py
"""
from __future__ import annotations

import io
import re
import sys
import zipfile
from pathlib import Path

from PIL import Image

_HERE = Path(__file__).resolve().parent
ATLAS_H = _HERE.parent / "assets" / "mob_atlas.h"
BUILDER = _HERE.parent / "assets" / "build_mob_atlas.py"

JAR_CANDIDATES = [
    Path.home() / ".gradle/caches/minecraft/net/minecraft/minecraft/1.11.2/minecraft-1.11.2.jar",
    _HERE.parents[2] / "java/Minecraft/run/gradle/caches/minecraft/net/minecraft/minecraft/1.11.2/minecraft-1.11.2.jar",
]
ENTITY = "assets/minecraft/textures/entity/"

# Skin-variant coverage contract (entity_render.c gm_entity_skin_for_name).
REQUIRED_SPRITES = {
    "pigman", "husk", "stray", "cave_spider", "mooshroom",
    # base skins those variants must NOT displace
    "zombie", "skeleton", "spider", "cow", "sheep", "sheep_fur",
}


def parse_atlas_header(text: str):
    w = int(re.search(r"#define CR_MOB_ATLAS_W (\d+)", text).group(1))
    h = int(re.search(r"#define CR_MOB_ATLAS_H (\d+)", text).group(1))
    sprites = []  # (name, x0, y0, x1, y1, nw, nh) in table order
    for m in re.finditer(
        r'\{ "(\w+)", (\d+), (\d+), (\d+), (\d+), (\d+), (\d+) \},', text
    ):
        sprites.append((m.group(1),) + tuple(int(g) for g in m.groups()[1:]))
    body = re.search(
        r"CR_MOB_ATLAS_RGBA\[\d+\] = \{(.*?)\};", text, re.S
    ).group(1)
    px = bytes(int(t) for t in re.findall(r"\d+", body))
    assert len(px) == w * h * 4, f"pixel blob {len(px)} != {w}x{h}x4"
    return w, h, sprites, px


def jar_member_map(builder_text: str):
    """MOB_SPRITES (name, jar member) pairs straight from the builder source."""
    block = re.search(r"MOB_SPRITES = sorted\(\[(.*?)\]", builder_text, re.S).group(1)
    block += "".join(re.findall(r"MOB_SPRITES\s*\+=\s*\[(.*?)\]", builder_text, re.S))
    return dict(re.findall(r'\(\s*"(\w+)",\s*"([\w/.]+)"\s*\)', block))


def main() -> int:
    jar = next((p for p in JAR_CANDIDATES if p.exists()), None)
    if jar is None:
        print("SKIP: minecraft-1.11.2.jar not found")
        return 0

    w, h, sprites, px = parse_atlas_header(ATLAS_H.read_text())
    members = jar_member_map(BUILDER.read_text())

    names = {s[0] for s in sprites}
    missing = REQUIRED_SPRITES - names
    if missing:
        print(f"FAIL: atlas missing required sprites: {sorted(missing)}")
        return 1

    bad = 0
    with zipfile.ZipFile(jar) as zf:
        for name, x0, y0, x1, y1, nw, nh in sprites:
            member = members.get(name)
            if member is None:
                print(f"FAIL: {name}: not in build_mob_atlas.py MOB_SPRITES")
                bad += 1
                continue
            img = Image.open(io.BytesIO(zf.read(ENTITY + member))).convert("RGBA")
            if (img.width, img.height) != (nw, nh) or (x1 - x0, y1 - y0) != (nw, nh):
                print(f"FAIL: {name}: jar {img.width}x{img.height} vs "
                      f"atlas native {nw}x{nh} rect {x1-x0}x{y1-y0}")
                bad += 1
                continue
            ref = img.tobytes()
            for row in range(nh):
                a = ((y0 + row) * w + x0) * 4
                if px[a:a + nw * 4] != ref[row * nw * 4:(row + 1) * nw * 4]:
                    print(f"FAIL: {name}: atlas texels differ from jar "
                          f"({member}) at row {row}")
                    bad += 1
                    break

    if bad:
        print(f"check_mob_atlas: {bad} FAILURES")
        return 1
    print(f"check_mob_atlas: PASS ({len(sprites)} sprites byte-identical to jar)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
