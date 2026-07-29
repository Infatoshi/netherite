#!/usr/bin/env python3
"""grind.py - rank every keyframe divergence and emit triptychs (oracle | magma | diff-heatmap x4).

The pixel whack-a-mole killer: run after every replay, work the ranked list top-down.

  uv run --no-project --with numpy --with pillow python grind.py [TAPE.jsonl]

Reads the tape's oracle frames dir + the newest matching out/tape_*/magma_frames.npy.
Writes grind/ranked.md and grind/triptych/f<idx>_t<tick>.png (both gitignored).
Frame i = tick i*20. Heatmap is |diff| mean/ch scaled x4 in the red channel.
"""
import numpy as np, os, sys
from PIL import Image

here = os.path.dirname(os.path.abspath(__file__))
tape = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
    here, "../tapes/20260721T215812Z_fast_s0_survival_default_rd8_77b5b462.jsonl")
name = os.path.basename(tape).rsplit(".jsonl", 1)[0]
tape_dir = os.path.join(os.path.dirname(tape), name + "_frames")
npy = os.path.join(here, "out", "tape_" + name, "magma_frames.npy")

c = np.load(npy, mmap_mode="r")
gd = os.path.join(here, "grind")
os.makedirs(os.path.join(gd, "triptych"), exist_ok=True)
rows = []
for i in range(c.shape[0]):
    t = i * 20
    op = os.path.join(tape_dir, f"f_{t:06d}.png")
    if not os.path.exists(op):
        continue
    o = np.asarray(Image.open(op).convert("RGB"), dtype=np.int16)
    cr = np.asarray(c[i], dtype=np.int16)
    d = np.abs(o - cr)
    mean = float(d.mean())
    bh, bw = o.shape[0] // 16, o.shape[1] // 16
    bm = d.mean(axis=2)[: bh * 16, : bw * 16].reshape(bh, 16, bw, -1).mean(axis=(1, 3))
    rows.append((i, t, mean, float(bm.max())))
    heat = np.clip(d.mean(axis=2) * 4, 0, 255).astype(np.uint8)
    heat_rgb = np.stack([heat, np.zeros_like(heat), np.zeros_like(heat)], axis=2)
    trip = np.concatenate([o.astype(np.uint8), cr.astype(np.uint8), heat_rgb], axis=1)
    Image.fromarray(trip).save(os.path.join(gd, "triptych", f"f{i:03d}_t{t}.png"))
rows.sort(key=lambda r: -r[2])
with open(os.path.join(gd, "ranked.md"), "w") as f:
    f.write(f"# Ranked keyframe diffs: {name}\n| frame | tick | mean/ch | worst 16x16 block |\n|--|--|--|--|\n")
    for i, t, m, wb in rows:
        f.write(f"| {i:3d} | {t:5d} | {m:6.2f} | {wb:7.2f} |\n")
for i, t, m, wb in rows[:15]:
    print(f"f{i:03d} t{t:5d} mean {m:6.2f} block {wb:7.2f}")
print(f"frames: {len(rows)}, >4/ch: {sum(1 for r in rows if r[2] > 4)}, "
      f"median {sorted(r[2] for r in rows)[len(rows)//2]:.2f}")
