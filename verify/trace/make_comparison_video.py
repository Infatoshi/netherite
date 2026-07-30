#!/usr/bin/env python3
"""Encode an oracle/C tape replay as a deterministic side-by-side MP4."""

import argparse
import json
import subprocess
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont


def font(size):
    path = Path("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf")
    return ImageFont.truetype(str(path), size) if path.exists() else ImageFont.load_default()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("tape", type=Path)
    parser.add_argument("replay_dir", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--start-tick", type=int, default=0)
    parser.add_argument("--end-tick", type=int)
    parser.add_argument("--fps", type=float, default=4.0)
    args = parser.parse_args()

    rows = []
    with args.tape.open() as stream:
        for line in stream:
            row = json.loads(line)
            tick = int(row.get("t", -1))
            if ("frame" in row and tick >= args.start_tick
                    and (args.end_tick is None or tick <= args.end_tick)):
                rows.append(row)
    c_frames = np.load(args.replay_dir / "magma_frames.npy", mmap_mode="r")
    c_ticks = np.load(args.replay_dir / "magma_frames.ticks.npy")
    c_index = {int(tick): i for i, tick in enumerate(c_ticks)}
    rows = [row for row in rows if int(row["t"]) in c_index]
    if not rows:
        raise SystemExit("no paired frames in requested tick range")

    with Image.open(rows[0]["frame"]) as first:
        width, height = first.size
    header_height = 32
    out_width, out_height = width * 2, height + header_height
    duration = len(rows) / args.fps
    print(f"[video] {len(rows)} paired frames, {duration:.2f}s, "
          f"{out_width}x{out_height} at {args.fps:g} fps")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    command = [
        "ffmpeg", "-hide_banner", "-loglevel", "warning", "-y",
        "-f", "rawvideo", "-pix_fmt", "rgb24",
        "-s:v", f"{out_width}x{out_height}", "-r", f"{args.fps:g}",
        "-i", "-", "-an", "-c:v", "libx264", "-preset", "medium",
        "-crf", "18", "-pix_fmt", "yuv420p", "-movflags", "+faststart",
        "-metadata", "title=Minecraft 1.11.2 oracle vs C reimplementation",
        str(args.output),
    ]
    label_font = font(17)
    time_font = font(14)
    proc = subprocess.Popen(command, stdin=subprocess.PIPE)
    try:
        for row in rows:
            tick = int(row["t"])
            with Image.open(row["frame"]) as oracle_image:
                oracle = np.asarray(oracle_image.convert("RGB"))
            magma = np.asarray(c_frames[c_index[tick]])
            if oracle.shape != magma.shape or oracle.shape[:2] != (height, width):
                raise RuntimeError(f"frame shape mismatch at tick {tick}: "
                                   f"{oracle.shape} vs {magma.shape}")
            canvas = Image.new("RGB", (out_width, out_height), "black")
            canvas.paste(Image.fromarray(oracle), (0, header_height))
            canvas.paste(Image.fromarray(magma), (width, header_height))
            draw = ImageDraw.Draw(canvas)
            draw.text((10, 6), "Oracle: Minecraft 1.11.2", font=label_font, fill="white")
            draw.text((width + 10, 6), "C reimplementation", font=label_font,
                      fill="white")
            elapsed = (tick - int(rows[0]["t"])) / 20.0
            stamp = f"tick {tick}   {elapsed // 60:02.0f}:{elapsed % 60:04.1f}"
            box = draw.textbbox((0, 0), stamp, font=time_font)
            draw.text((out_width - (box[2] - box[0]) - 10, 8), stamp,
                      font=time_font, fill=(210, 210, 210))
            proc.stdin.write(np.asarray(canvas, dtype=np.uint8).tobytes())
    finally:
        if proc.stdin:
            proc.stdin.close()
    if proc.wait() != 0:
        raise SystemExit(f"ffmpeg exited with status {proc.returncode}")
    print(f"[video] wrote {args.output}")


if __name__ == "__main__":
    main()
