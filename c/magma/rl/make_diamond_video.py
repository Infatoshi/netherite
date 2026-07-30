"""Replay and annotate the full natural-survival diamond-tools trace.

The exact recorded action stream is replayed through magma. Sparse capture
keeps tick-numbered frames synchronized while compressing the 23k-tick run to
roughly one minute. The right panel is the agent's 64x36 semantic camera.

Run: cd c/magma && uv run --no-project --with numpy,pillow python \
     rl/make_diamond_video.py 2
"""
import json
import os
import shutil
import subprocess
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

from make_videos import MAGMA, OUT, COLORS, CAM_W, CAM_H, encode

SEED = int(sys.argv[1]) if len(sys.argv) > 1 else 2
FRAME_EVERY = int(os.environ.get("DIAMOND_VIDEO_EVERY", "20"))
FPS = int(os.environ.get("DIAMOND_VIDEO_FPS", "20"))
GAME_W, GAME_H = 854, 480
PANEL_SCALE = 8

PALETTE = dict(COLORS)
PALETTE.update({
    15: (194, 166, 132),       # iron ore
    16: (245, 188, 55),        # coal ore
    56: (70, 235, 235),        # diamond ore
    58: (222, 148, 70),        # crafting table
    61: (92, 92, 92),          # furnace
})

STAGE_LABELS = {
    "chop": "Find and chop trees",
    "craft_kit": "Craft planks, sticks, and a table",
    "place_table": "Place the first crafting table",
    "wooden_pick": "Craft a wooden pickaxe",
    "dig": "Dig down and collect cobblestone",
    "mine_coal": "Locate and mine natural coal",
    "torches": "Craft and place torches",
    "cobble_bank": "Bank stone for the furnace",
    "iron_kit": "Craft stone pickaxes and field tables",
    "iron_mine": "Mine six natural iron ore",
    "smelt": "Build a furnace and smelt six ingots",
    "iron_pick": "Craft two iron pickaxes",
    "diamond_pick": "Mine three diamonds and craft the diamond pick",
    "diamond_mine": "Use the diamond pick to mine eight more diamonds",
    "diamond_tools": "Craft diamond shovel, axe, hoe, and sword",
}


def fonts():
    candidates = (
        "/System/Library/Fonts/Supplemental/Arial Bold.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
    )
    for path in candidates:
        try:
            bold = ImageFont.truetype(path, 22)
            small = ImageFont.truetype(path, 16)
            return bold, small
        except OSError:
            pass
    fallback = ImageFont.load_default()
    return fallback, fallback


def panel_img(obs):
    cam = np.asarray(obs["cam"], dtype=np.int32).reshape(CAM_H, CAM_W)
    dep = np.asarray(obs["depth"], dtype=np.float64).reshape(CAM_H, CAM_W)
    img = np.full((CAM_H, CAM_W, 3), (90, 90, 90), np.uint8)
    for bid, color in PALETTE.items():
        img[cam == bid] = color
    solid = cam != 0
    shade = (1.0 - dep / 512.0)[..., None]
    img[solid] = (img[solid] * shade[solid]).astype(np.uint8)
    edge = np.asarray(obs["edge"], np.bool_).reshape(CAM_H, CAM_W)
    img[edge] = (img[edge] * 0.55).astype(np.uint8)
    return np.repeat(np.repeat(img, PANEL_SCALE, 0), PANEL_SCALE, 1)


def stage_at(meta, tick):
    for index, stage in enumerate(meta["stages"], 1):
        if tick <= stage["end"]:
            return index, stage["name"]
    return len(meta["stages"]), "diamond_tools"


def inventory_lines(obs):
    base = obs["inv_counts"]
    iron = obs["inv_iron"]
    diamond = obs["inv_diamond"]
    line1 = (f"logs {base[0]}   planks {base[1]}   sticks {base[2]}   "
             f"cobble {base[3]}   coal {base[7]}   torches {base[8]}")
    tools = ("pick", "shovel", "axe", "hoe", "sword")
    owned = ", ".join(name for name, count in zip(tools, diamond[1:])
                      if count) or "none"
    line2 = (f"iron ore {iron[1]}   ingots {iron[2]}   iron picks {iron[3]}   "
             f"diamonds {diamond[0]}   diamond tools: {owned}")
    return line1, line2


def main():
    stem = os.path.join(OUT, f"diamond_actions_s{SEED}")
    with open(stem + ".json") as f:
        actions = json.load(f)
    with open(stem + ".meta.json") as f:
        meta = json.load(f)
    final_capture = ((len(actions) + FRAME_EVERY - 1) // FRAME_EVERY) \
        * FRAME_EVERY
    replay_actions = actions + [{} for _ in
                                range(final_capture - len(actions) + 1)]
    fdir = os.path.join(OUT, f"diamond_frames_s{SEED}")
    cdir = os.path.join(OUT, f"diamond_composite_s{SEED}")
    for path in (fdir, cdir):
        shutil.rmtree(path, ignore_errors=True)
        os.makedirs(path)

    proc = subprocess.Popen(
        [os.path.join(MAGMA, "magma_game"), "--rl", "--render", "off",
         "--pace", "unlimited", "--seed", str(SEED), "--mobs", "off",
         "--width", str(GAME_W), "--height", str(GAME_H),
         "--frames-out", fdir, "--frame-offset", "0",
         "--frame-every", str(FRAME_EVERY)],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, text=True, bufsize=1)

    def read_obs():
        while True:
            line = proc.stdout.readline()
            if not line:
                detail = proc.stderr.read()
                raise RuntimeError(f"diamond replay died: {detail}")
            if line.startswith("{"):
                return json.loads(line)

    sampled = {}
    read_obs()
    for tick, original in enumerate(replay_actions):
        action = dict(original)
        action["cam"] = 1 if tick % FRAME_EVERY == 0 else 0
        proc.stdin.write(json.dumps(action) + "\n")
        proc.stdin.flush()
        obs = read_obs()
        if tick % FRAME_EVERY == 0:
            sampled[tick] = obs
    proc.stdin.close()
    rc = proc.wait(timeout=120)
    if rc:
        raise RuntimeError(f"diamond replay rc={rc}: {proc.stderr.read()}")
    print(f"replayed {len(actions)} objective ticks + "
          f"{len(replay_actions) - len(actions)} finale ticks; "
          f"sampled {len(sampled)} frames", flush=True)

    bold, small = fonts()
    gap = 10
    panel_w, panel_h = CAM_W * PANEL_SCALE, CAM_H * PANEL_SCALE
    width, height = GAME_W + gap + panel_w, 550
    frame_ticks = sorted(int(name[6:12]) for name in os.listdir(fdir)
                         if name.startswith("frame_"))
    for ordinal, tick in enumerate(frame_ticks):
        game = Image.open(os.path.join(fdir, f"frame_{tick:06d}.ppm"))
        canvas = Image.new("RGB", (width, height), (15, 17, 20))
        canvas.paste(game, (0, height - GAME_H))
        obs = sampled[tick]
        canvas.paste(Image.fromarray(panel_img(obs)),
                     (GAME_W + gap, height - panel_h))
        draw = ImageDraw.Draw(canvas)
        index, name = stage_at(meta, tick + 1)
        title = f"{index}/{len(meta['stages'])}  {STAGE_LABELS[name]}"
        draw.text((12, 8), title, fill=(255, 232, 132), font=bold)
        line1, line2 = inventory_lines(obs)
        draw.text((12, 36), line1, fill=(215, 215, 215), font=small)
        draw.text((12, 56), line2, fill=(150, 235, 235), font=small)
        draw.text((GAME_W + gap, 8), "agent semantic camera (64x36)",
                  fill=(190, 190, 190), font=small)
        draw.text((GAME_W + gap, 30),
                  "diamond ore = cyan   coal = gold", fill=(100, 235, 235),
                  font=small)
        canvas.save(os.path.join(cdir, f"c_{ordinal:06d}.png"))
        if ordinal and ordinal % 300 == 0:
            print(f"composed {ordinal}/{len(frame_ticks)} frames", flush=True)

    last = len(frame_ticks) - 1
    src = os.path.join(cdir, f"c_{last:06d}.png")
    for offset in range(1, 3 * FPS + 1):
        shutil.copyfile(src, os.path.join(cdir, f"c_{last + offset:06d}.png"))
    mp4 = os.path.join(OUT, f"diamond_tools_s{SEED}.mp4")
    encode(cdir, mp4, fps=FPS)
    print(f"wrote {mp4}", flush=True)


if __name__ == "__main__":
    main()
