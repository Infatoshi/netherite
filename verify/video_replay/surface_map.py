#!/usr/bin/env python3
"""Render a CRWS dump as a spawn-landmark map for video tape recovery."""
import argparse
import json
import pathlib
import struct

from PIL import Image, ImageDraw


COLORS = {
    0: (20, 22, 28), 2: (88, 158, 67), 3: (112, 82, 58),
    8: (48, 101, 202), 9: (48, 101, 202), 12: (218, 205, 143),
    17: (112, 79, 48), 18: (43, 105, 43), 31: (103, 153, 67),
    37: (242, 221, 68), 38: (240, 240, 240), 161: (38, 96, 38),
}


def load_crws(path):
    with path.open("rb") as stream:
        if stream.read(4) != b"CRWS":
            raise ValueError("not a CRWS world dump")
        stream.read(8)
        cx0, cz0, nx, nz = struct.unpack("<4i", stream.read(16))
        chunks = {}
        for ix in range(nx):
            for iz in range(nz):
                raw = stream.read(16 * 256 * 16 * 2)
                stream.read(16 * 16 * 4)
                if len(raw) != 16 * 256 * 16 * 2:
                    raise ValueError("truncated CRWS dump")
                chunks[cx0 + ix, cz0 + iz] = memoryview(raw).cast("H")
    return cx0, cz0, nx, nz, chunks


def top(chunks, x, z):
    cx, cz = x // 16, z // 16
    block = chunks.get((cx, cz))
    if block is None:
        return 0, 0
    lx, lz = x - cx * 16, z - cz * 16
    base = lx * 4096 + lz * 256
    for y in range(255, -1, -1):
        block_id = block[base + y] >> 4
        if block_id:
            return block_id, y
    return 0, 0


def log_span(chunks, x, z):
    cx, cz = x // 16, z // 16
    block = chunks.get((cx, cz))
    if block is None:
        return None
    lx, lz = x - cx * 16, z - cz * 16
    base = lx * 4096 + lz * 256
    ys = [y for y in range(256) if (block[base + y] >> 4) == 17]
    return (min(ys), max(ys)) if ys else None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("world")
    parser.add_argument("--out", required=True)
    parser.add_argument("--spawn-x", type=int, required=True)
    parser.add_argument("--spawn-z", type=int, required=True)
    parser.add_argument("--scale", type=int, default=5)
    args = parser.parse_args()
    cx0, cz0, nx, nz, chunks = load_crws(pathlib.Path(args.world))
    x0, z0 = cx0 * 16, cz0 * 16
    width, height = nx * 16, nz * 16
    image = Image.new("RGB", (width, height))
    pixels = image.load()
    logs = []
    for pz in range(height):
        for px in range(width):
            x, z = x0 + px, z0 + pz
            block_id, y = top(chunks, x, z)
            base = COLORS.get(block_id, (115, 115, 115))
            light = max(0.68, min(1.25, 0.85 + (y - 63) * 0.018))
            pixels[px, pz] = tuple(min(255, round(c * light)) for c in base)
            span = log_span(chunks, x, z)
            if span:
                logs.append((x, z, span[0], span[1]))
    scale = max(1, args.scale)
    image = image.resize((width * scale, height * scale), Image.Resampling.NEAREST)
    draw = ImageDraw.Draw(image)
    for x, z, _, _ in logs:
        px, pz = (x - x0) * scale, (z - z0) * scale
        draw.rectangle((px, pz, px + scale - 1, pz + scale - 1),
                       fill=COLORS[17])
    sx = (args.spawn_x - x0) * scale
    sz = (args.spawn_z - z0) * scale
    draw.rectangle((sx - scale, sz - scale, sx + scale, sz + scale),
                   outline=(255, 0, 255), width=max(1, scale // 2))
    draw.rectangle(((args.spawn_x - 10 - x0) * scale,
                    (args.spawn_z - 10 - z0) * scale,
                    (args.spawn_x - 6 - x0 + 1) * scale - 1,
                    (args.spawn_z - 6 - z0 + 1) * scale - 1),
                   outline=(255, 255, 255), width=max(1, scale // 2))
    pathlib.Path(args.out).parent.mkdir(parents=True, exist_ok=True)
    image.save(args.out)
    print(json.dumps({"bounds": [x0, z0, x0 + width - 1, z0 + height - 1],
                      "spawn": [args.spawn_x, args.spawn_z], "surface_logs": logs},
                     sort_keys=True))


if __name__ == "__main__":
    main()
