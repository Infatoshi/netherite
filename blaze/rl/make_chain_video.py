"""Compose the spawn-to-torch chain mp4 (game render + agent camera panel).

Replays rl/out/chain_actions_s<seed>.json through ONE magma process in rl
mode with --frames-out: the game frames and the semantic-camera obs come from
the same ticks, so the two panels are exactly synchronized. Stage labels are
derived live from inventory transitions.

Run (anvil): cd magma && uv run --no-project --with numpy,pillow python rl/make_chain_video.py [seed]
"""
import json
import os
import shutil
import subprocess
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

from make_videos import MAGMA, OUT, COLORS, CAM_W, CAM_H, SCALE, encode

SEED = int(sys.argv[1]) if len(sys.argv) > 1 else 10
SUFFIX = sys.argv[2] if len(sys.argv) > 2 else ""   # e.g. "_learned"
FPS = 30                      # 1.5x real time
GAME_W, GAME_H = 854, 480

PALETTE = dict(COLORS)
PALETTE[16] = (255, 190, 40)  # coal ore: the target, make it pop
PALETTE[58] = (222, 148, 70)  # crafting table

# inv_counts order (rl_mode.c rl_inv_ids)
INV_NAMES = ("log", "plank", "stick", "cobble", "table", "wood pick",
             "stone pick", "coal", "torch")


def stage_label(inv, container):
    log, plank, stick, cobble, table, wpick, spick, coal, torch = inv
    if torch:
        return "7/7 TORCHES CRAFTED - chain complete"
    if coal:
        return "6/7 coal mined -> crafting torches"
    if cobble >= 3:
        return "5/7 digging: hunting the scanned coal ore"
    if wpick:
        return "4/7 wooden pickaxe -> digging to stone"
    if container:
        return "3/7 crafting table open (3x3): pickaxe"
    if table or (not table and plank and stick and not wpick):
        return "2/7 crafting: planks, sticks, table"
    if log:
        return "1/7 chopping logs (bare hands)"
    return "0/7 finding a tree"


def panel_img(obs):
    cam = np.asarray(obs["cam"], dtype=np.int32).reshape(CAM_H, CAM_W)
    dep = np.asarray(obs["depth"], dtype=np.float64).reshape(CAM_H, CAM_W)
    img = np.full((CAM_H, CAM_W, 3), (90, 90, 90), np.uint8)
    for bid, col in PALETTE.items():
        img[cam == bid] = col
    solid = cam != 0
    shade = (1.0 - dep / 512.0)[..., None]
    img[solid] = (img[solid] * shade[solid]).astype(np.uint8)
    edge = np.asarray(obs["edge"], np.bool_).reshape(CAM_H, CAM_W)
    img[edge] = (img[edge] * 0.55).astype(np.uint8)
    return np.repeat(np.repeat(img, SCALE, 0), SCALE, 1)


def main():
    acts = json.load(open(os.path.join(
        OUT, f"chain_actions_s{SEED}{SUFFIX}.json")))
    fdir = os.path.join(OUT, f"chain_frames_s{SEED}{SUFFIX}")
    cdir = os.path.join(OUT, f"chain_comp_s{SEED}{SUFFIX}")
    for d in (fdir, cdir):
        shutil.rmtree(d, ignore_errors=True)
        os.makedirs(d)

    proc = subprocess.Popen(
        [os.path.join(MAGMA, "magma_game"), "--rl", "--render", "off",
         "--pace", "unlimited", "--seed", str(SEED), "--mobs", "off",
         "--width", str(GAME_W), "--height", str(GAME_H),
         "--frames-out", fdir, "--frame-offset", "0", "--frame-every", "1"],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL, text=True, bufsize=1)

    def read_obs():
        while True:
            ln = proc.stdout.readline()
            if not ln:
                raise RuntimeError("rl replay died")
            if ln.startswith("{"):
                return json.loads(ln)

    obs_by_t = {}
    obs = read_obs()
    obs_by_t[obs["t"]] = obs
    for a in acts:
        proc.stdin.write(json.dumps(a) + "\n")
        proc.stdin.flush()
        obs = read_obs()
        obs_by_t[obs["t"]] = obs
    proc.stdin.close()
    proc.wait(timeout=30)
    print(f"replayed {len(acts)} ticks, frames in {fdir}", flush=True)

    try:
        font = ImageFont.truetype(
            "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 22)
        small = ImageFont.truetype(
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 17)
    except OSError:
        font = small = ImageFont.load_default()

    gap = 10
    w, h = GAME_W + gap + CAM_W * SCALE, 540
    n = 0
    for k in sorted(int(f[6:12]) for f in os.listdir(fdir)
                    if f.startswith("frame_")):
        game = Image.open(os.path.join(fdir, f"frame_{k:06d}.ppm"))
        canvas = Image.new("RGB", (w, h), (16, 16, 16))
        canvas.paste(game, (0, h - GAME_H))
        obs = obs_by_t.get(k + 1)
        if obs is not None:
            canvas.paste(Image.fromarray(panel_img(obs)),
                         (GAME_W + gap, h - CAM_H * SCALE))
            d = ImageDraw.Draw(canvas)
            inv = obs["inv_counts"]
            d.text((12, 8), stage_label(inv, obs["container"]),
                   fill=(255, 230, 120), font=font)
            items = "  ".join(f"{INV_NAMES[i]}:{c}"
                              for i, c in enumerate(inv) if c)
            d.text((12, 36), items or "inventory: empty",
                   fill=(200, 200, 200), font=small)
            d.text((GAME_W + gap, 8), "agent obs (64x36 semantic camera)",
                   fill=(150, 150, 150), font=small)
            d.text((GAME_W + gap, 30), "coal ore = gold",
                   fill=(255, 190, 40), font=small)
        canvas.save(os.path.join(cdir, f"c_{k:06d}.png"))
        n += 1
        if n % 400 == 0:
            print(f"composed {n} frames", flush=True)

    # freeze the finale (the 7/7 torches state) for 3 seconds
    last = max(int(f[2:8]) for f in os.listdir(cdir))
    src = os.path.join(cdir, f"c_{last:06d}.png")
    for j in range(1, 3 * FPS + 1):
        shutil.copyfile(src, os.path.join(cdir, f"c_{last + j:06d}.png"))

    mp4 = os.path.join(OUT, f"chain_s{SEED}{SUFFIX}.mp4")
    encode(cdir, mp4, fps=FPS)
    print("wrote", mp4, flush=True)


if __name__ == "__main__":
    main()
