#!/usr/bin/env python3
"""leaves_oak 16x16 in stitched atlas must match jar texture bytes."""
import io
import zipfile
from pathlib import Path

from _common import parse_args, finish


def main():
    args = parse_args()
    repo = Path(args.repo)
    try:
        from PIL import Image
        import numpy as np
    except ImportError:
        finish(False, reason="need pillow+numpy")

    jar_candidates = [
        repo
        / "java/Minecraft/run/gradle/caches/minecraft/net/minecraft/minecraft/1.11.2/minecraft-1.11.2.jar",
        Path.home()
        / ".gradle/caches/minecraft/net/minecraft/minecraft/1.11.2/minecraft-1.11.2.jar",
    ]
    jar = next((j for j in jar_candidates if j.is_file()), None)
    if not jar:
        finish(False, reason="minecraft-1.11.2.jar not found")

    with zipfile.ZipFile(jar) as z:
        raw = z.read("assets/minecraft/textures/blocks/leaves_oak.png")
    jar_px = np.asarray(Image.open(io.BytesIO(raw)).convert("RGBA"))

    # Prefer rebuilt atlas dump; else rebuild
    dump = Path("/tmp/magma_atlas_board.png")
    build = repo / "magma/assets/build_atlas.py"
    # build_atlas dumps to a fixed path; run and read atlas_gen coords
    import subprocess, sys, re, os

    env = os.environ.copy()
    r = subprocess.run(
        [sys.executable, str(build)],
        cwd=str(repo / "magma"),
        capture_output=True,
        text=True,
        env=env,
    )
    # parse dump path from stdout
    dump_path = None
    for line in (r.stdout + r.stderr).splitlines():
        if "dumped atlas PNG" in line or "DUMP" in line:
            pass
    # default path in build_atlas.py
    candidates = [
        Path("/tmp/grok-goal-a3c013c69dd0/implementer/magma_atlas.png"),
        Path("/tmp/magma_atlas.png"),
    ]
    atlas_png = next((c for c in candidates if c.is_file()), None)
    if not atlas_png:
        finish(False, reason="atlas png missing after build_atlas", log=(r.stdout + r.stderr)[-500:])

    atlas = np.asarray(Image.open(atlas_png).convert("RGBA"))
    # rect from atlas_gen.h
    h = (repo / "magma/assets/atlas_gen.h").read_text()
    m = re.search(r'"leaves_oak"\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)', h)
    if not m:
        finish(False, reason="leaves_oak rect not found in atlas_gen.h")
    x0, y0, x1, y1 = map(int, m.groups())
    tile = atlas[y0:y1, x0:x1]
    if tile.shape != jar_px.shape:
        # animated: first frame only already in jar_px if 16x16
        if jar_px.shape[0] != 16:
            jar_px = jar_px[:16, :16]
    ok = tile.shape == jar_px.shape and np.array_equal(tile, jar_px)
    finish(
        bool(ok),
        jar=str(jar),
        atlas_png=str(atlas_png),
        rect=[x0, y0, x1, y1],
        tile_shape=list(tile.shape),
        jar_shape=list(jar_px.shape),
        n_diff=int(np.sum(tile != jar_px)) if tile.shape == jar_px.shape else -1,
    )


if __name__ == "__main__":
    main()
