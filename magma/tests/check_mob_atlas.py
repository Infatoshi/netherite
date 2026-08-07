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
The standalone repeating end-crystal beam texture is checked byte-for-byte too.

Requires assets/mob_atlas.h (generated). Clean trees must run:
  uv run --no-project --with pillow python assets/build_mob_atlas.py
before game/tests. Jar discovery matches assets/mc_jar.py (MC_JAR, gradle).

Run: uv run --no-project --with pillow python tests/check_mob_atlas.py
"""
from __future__ import annotations

import io
import os
import re
import sys
import zipfile
from pathlib import Path

from PIL import Image

_HERE = Path(__file__).resolve().parent
_MAGMA = _HERE.parent
ATLAS_H = _MAGMA / "assets" / "mob_atlas.h"
BUILDER = _MAGMA / "assets" / "build_mob_atlas.py"
_REPO = _MAGMA.parent

# Same discovery order as assets/mc_jar.py (MC_JAR + gradle + launchers),
# plus common fleet checkouts when this is a /tmp worktree without gradle cache.
def find_jar() -> Path | None:
    cands: list[Path] = []
    if os.environ.get("MC_JAR"):
        cands.append(Path(os.environ["MC_JAR"]))
    rel = Path("java/Minecraft/run/gradle/caches/minecraft/net/minecraft/minecraft/1.11.2/minecraft-1.11.2.jar")
    cands.append(_REPO / rel)
    cands.append(
        Path.home()
        / ".gradle/caches/minecraft/net/minecraft/minecraft/1.11.2/minecraft-1.11.2.jar"
    )
    for extra in (
        Path.home() / "dev/netherite" / rel,
        Path.home() / "dev/minecraft/mc-1.11.2-env" / rel,
        Path("/tmp/netherite-ui-entities") / rel,
    ):
        cands.append(extra)
    for pat in (
        "~/.local/share/PrismLauncher/libraries/com/mojang/minecraft/"
        "1.11.2/minecraft-1.11.2-client.jar",
        "~/Library/Application Support/PrismLauncher/libraries/com/"
        "mojang/minecraft/1.11.2/minecraft-1.11.2-client.jar",
        "~/.minecraft/versions/1.11.2/1.11.2.jar",
    ):
        p = Path(os.path.expanduser(pat))
        if p.exists():
            cands.append(p)
    for c in cands:
        if c.is_file():
            return c
    return None


ENTITY = "assets/minecraft/textures/entity/"

# Coverage contract: skin variants + pixel-path sheets that entity_render needs.
REQUIRED_SPRITES = {
    "pigman", "husk", "stray", "cave_spider", "mooshroom",
    # base skins those variants must NOT displace
    "zombie", "skeleton", "spider", "cow", "sheep", "sheep_fur",
    # interactive pixel path (4bc712e+ / explosion.png LARGE)
    "slime", "dragon", "dragon_exploding", "particles", "explosion",
    "villager", "villager_farmer", "villager_librarian",
    "villager_priest", "villager_smith", "villager_butcher",
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
    bw = int(re.search(r"#define CR_ENDERCRYSTAL_BEAM_W (\d+)", text).group(1))
    bh = int(re.search(r"#define CR_ENDERCRYSTAL_BEAM_H (\d+)", text).group(1))
    beam_body = re.search(
        r"CR_ENDERCRYSTAL_BEAM_RGBA\[\d+\] = \{(.*?)\};", text, re.S
    ).group(1)
    beam_px = bytes(int(t) for t in re.findall(r"\d+", beam_body))
    assert len(beam_px) == bw * bh * 4, f"beam blob {len(beam_px)} != {bw}x{bh}x4"
    return w, h, sprites, px, bw, bh, beam_px


def jar_member_map(builder_text: str) -> dict[str, str]:
    """MOB_SPRITES (name, jar member) pairs from the builder source.

    Members may be relative to entity/ or use ../particle/... for the particle
    sheet. Also accept assets/... absolute-in-jar paths.
    """
    block = re.search(r"MOB_SPRITES = sorted\(\[(.*?)\]", builder_text, re.S)
    if not block:
        return {}
    text = block.group(1)
    text += "".join(re.findall(r"MOB_SPRITES\s*\+=\s*\[(.*?)\]", builder_text, re.S))
    # Allow dots in ../particle/particles.png
    return dict(re.findall(r'\(\s*"(\w+)",\s*"([^"]+)"\s*\)', text))


def jar_path_for_member(member: str) -> str:
    if member.startswith("assets/"):
        return member
    if member.startswith("../"):
        return "assets/minecraft/textures/" + member[3:]
    return ENTITY + member


def main() -> int:
    if not ATLAS_H.is_file():
        print(
            "FAIL: assets/mob_atlas.h missing. Generate with:\n"
            "  uv run --no-project --with pillow python assets/build_mob_atlas.py\n"
            "(set MC_JAR if the jar is not in the gradle cache)"
        )
        return 1

    jar = find_jar()
    if jar is None:
        print(
            "SKIP: minecraft-1.11.2.jar not found "
            "(set MC_JAR or run scripts/bootstrap_oracle.sh)"
        )
        return 0

    w, h, sprites, px, bw, bh, beam_px = parse_atlas_header(ATLAS_H.read_text())
    members = jar_member_map(BUILDER.read_text())

    names = {s[0] for s in sprites}
    missing = REQUIRED_SPRITES - names
    if missing:
        print(f"FAIL: atlas missing required sprites: {sorted(missing)}")
        print("  rebuild: uv run --no-project --with pillow python assets/build_mob_atlas.py")
        return 1

    # Builder must list every required sprite (guards silent drop from MOB_SPRITES).
    builder_missing = REQUIRED_SPRITES - set(members)
    if builder_missing:
        print(f"FAIL: build_mob_atlas.py missing MOB_SPRITES: {sorted(builder_missing)}")
        return 1

    bad = 0
    with zipfile.ZipFile(jar) as zf:
        for name, x0, y0, x1, y1, nw, nh in sprites:
            member = members.get(name)
            if member is None:
                print(f"FAIL: {name}: not in build_mob_atlas.py MOB_SPRITES")
                bad += 1
                continue
            path = jar_path_for_member(member)
            try:
                raw = zf.read(path)
            except KeyError:
                print(f"FAIL: {name}: jar missing {path}")
                bad += 1
                continue
            img = Image.open(io.BytesIO(raw)).convert("RGBA")
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
                          f"({path}) at row {row}")
                    bad += 1
                    break
        beam_path = ENTITY + "endercrystal/endercrystal_beam.png"
        beam = Image.open(io.BytesIO(zf.read(beam_path))).convert("RGBA")
        if beam.size != (bw, bh) or beam.tobytes() != beam_px:
            print(f"FAIL: standalone end-crystal beam differs from jar ({beam_path})")
            bad += 1

    if bad:
        print(f"check_mob_atlas: {bad} FAILURES")
        return 1
    print(f"check_mob_atlas: PASS ({len(sprites)} sprites byte-identical to jar)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
