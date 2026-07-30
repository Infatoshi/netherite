#!/usr/bin/env python3
"""FACES[].shade must match Forge LightUtil.diffuseLight for EnumFacing."""
import re
from pathlib import Path
from _common import parse_args, finish

# net.minecraftforge.client.model.pipeline.LightUtil.diffuseLight
EXPECTED = {
    "DOWN": 0.5,
    "UP": 1.0,
    "NORTH": 0.8,
    "SOUTH": 0.8,
    "WEST": 0.6,
    "EAST": 0.6,
}


def main():
    args = parse_args()
    repo = Path(args.repo)
    src = (repo / "magma/world/mesh_mc.c").read_text()
    # parse FACES array shades in order DOWN..EAST
    m = re.search(
        r"static const Face FACES\[6\]\s*=\s*\{(.*?)\n\};",
        src,
        re.S,
    )
    if not m:
        finish(False, reason="FACES array not found")
    body = m.group(1)
    shades = [float(x) for x in re.findall(r"\}\s*,\s*([0-9.]+)f\s*,", body)]
    # pattern: { {n}, shade, { corners
    shades = [float(x) for x in re.findall(r"\{\s*\{\s*[^}]+\}\s*,\s*([0-9.]+)f", body)]
    if len(shades) != 6:
        # fallback simpler
        shades = [float(x) for x in re.findall(r"0\.[0-9]+f|1\.0f", body)]
        # take first 6 that look like shade slots - fragile
    names = ["DOWN", "UP", "NORTH", "SOUTH", "WEST", "EAST"]
    if len(shades) < 6:
        finish(False, reason="could not parse 6 shades", body_head=body[:200], shades=shades)
    got = {names[i]: shades[i] for i in range(6)}
    ok = all(abs(got[k] - EXPECTED[k]) < 1e-6 for k in EXPECTED)
    finish(ok, got=got, expected=EXPECTED)


if __name__ == "__main__":
    main()
