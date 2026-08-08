"""Compose the look-at-tree learning mp4: real render + what the agent sees.

Per chosen episode (early/mid/late off the env-0 reward series):
  1. script-mode replay of the recorded action stream -> 854x480 game frames
     (deterministic: same seed, same tick order as rl mode)
  2. rl-mode rerun of the SAME actions -> the 64x36 semantic camera (id +
     depth) the policy actually observed, colorized and upscaled x10
  3. side-by-side composite per tick, ffmpeg encode, label + concat

Game frame at script tick k shows post-tick state t=k+1, so it pairs with rl
obs t=k+1.

Run (anvil): cd magma && uv run --no-project --with numpy,pillow python rl/make_videos.py
Then scp the mp4 to the macbook (anvil is headless).
"""
import json
import os
import subprocess
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
MAGMA = os.path.join(os.path.dirname(os.path.dirname(HERE)), "magma")
OUT = os.path.join(HERE, "out")
FPS = 10  # 2x slow motion (20 tps game time)
CAM_W, CAM_H, SCALE = 64, 36, 10

# block id -> rgb for the semantic panel (unknown ids: dark gray)
COLORS = {
    0: (110, 165, 255), 1: (128, 128, 128), 2: (95, 159, 53),
    3: (134, 96, 67), 4: (100, 100, 100), 7: (40, 40, 40),
    8: (47, 90, 200), 9: (47, 90, 200), 10: (230, 120, 30),
    11: (230, 120, 30), 12: (219, 211, 160), 13: (136, 126, 126),
    14: (143, 140, 125), 15: (135, 130, 126), 16: (105, 105, 105),
    17: (155, 106, 56), 18: (32, 105, 28), 24: (215, 205, 158),
    31: (74, 132, 50), 32: (123, 79, 25), 37: (255, 220, 60),
    38: (215, 60, 60), 78: (240, 248, 255), 79: (145, 180, 235),
    82: (158, 164, 176), 86: (222, 126, 34), 99: (140, 110, 90),
    100: (200, 60, 60), 106: (25, 90, 25), 175: (60, 140, 60),
}


def pick_episodes(env0):
    """(early, mid, late) episode indices off the env-0 reward series."""
    early = 0
    late_window = env0[-30:]
    late = len(env0) - 30 + int(np.argmax(late_window))
    # mid = literally the middle of training (a "first reward > x" pick just
    # finds a lucky early episode, not mid-learning behavior)
    mid = len(env0) // 2
    return early, mid, late


def load_actions(end):
    acts = []
    with open(os.path.join(OUT, "env0_script.jsonl")) as f:
        for line in f:
            ev = json.loads(line)
            if ev["tick"] >= end:
                break
            acts.append({k: v for k, v in ev.items()
                         if k not in ("tick", "type")})
    return acts


def game_frames(marks, start, end, tag):
    fdir = os.path.join(OUT, f"frames_{tag}")
    os.makedirs(fdir, exist_ok=True)
    # the recorded stream covers ALL episodes; script mode refuses events
    # beyond --ticks, so cut it at this episode's end
    script = os.path.join(OUT, f"script_{tag}.jsonl")
    with open(os.path.join(OUT, "env0_script.jsonl")) as src, \
         open(script, "w") as dst:
        for line in src:
            if json.loads(line)["tick"] < end:
                dst.write(line)
    cmd = [os.path.join(MAGMA, "magma_game"),
           "--headless", "--world", "default", "--seed", str(marks["seed"]),
           "--ticks", str(end), "--width", "854", "--height", "480",
           "--script", script,
           "--state-out", os.path.join(OUT, f"state_{tag}.jsonl"),
           "--mobs", "off", "--view-distance", "6",
           "--frames-out", fdir, "--frame-offset", str(start)]
    r = subprocess.run(cmd, cwd=MAGMA, capture_output=True, text=True)
    if r.returncode != 0:
        sys.stderr.write(r.stdout + r.stderr)
        raise RuntimeError(f"replay {tag} rc={r.returncode}")
    return fdir


def cam_frames(marks, start, end):
    """Rerun the recorded actions through rl mode; return {t: rgb array}."""
    proc = subprocess.Popen(
        [os.path.join(MAGMA, "magma_game"), "--rl", "--render", "off",
         "--pace", "unlimited", "--seed", str(marks["seed"]),
         "--mobs", "off"],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL, text=True, bufsize=1)

    def read_obs():
        while True:
            line = proc.stdout.readline()
            if not line:
                raise RuntimeError("rl rerun died")
            if line.startswith("{"):
                return json.loads(line)

    panels = {}

    def keep(obs):
        t = obs["t"]
        if not (start + 1 <= t <= end):
            return
        cam = np.asarray(obs["cam"], dtype=np.int32).reshape(CAM_H, CAM_W)
        dep = np.asarray(obs["depth"], dtype=np.float64).reshape(CAM_H, CAM_W)
        img = np.full((CAM_H, CAM_W, 3), (90, 90, 90), np.uint8)
        for bid, col in COLORS.items():
            img[cam == bid] = col
        solid = cam != 0
        shade = (1.0 - dep / 512.0)[..., None]
        img[solid] = (img[solid] * shade[solid]).astype(np.uint8)
        if "edge" in obs:  # block borders from the raycaster
            edge = np.asarray(obs["edge"], np.bool_).reshape(CAM_H, CAM_W)
            img[edge] = (img[edge] * 0.55).astype(np.uint8)
        panels[t] = np.repeat(np.repeat(img, SCALE, 0), SCALE, 1)

    keep(read_obs())
    for act in load_actions(end):
        proc.stdin.write(json.dumps(act) + "\n")
        proc.stdin.flush()
        obs = read_obs()
        keep(obs)
        if obs["t"] >= end:
            break
    proc.stdin.close()
    proc.wait(timeout=10)
    return panels


def compose(fdir, panels, start, end, tag):
    cdir = os.path.join(OUT, f"comp_{tag}")
    os.makedirs(cdir, exist_ok=True)
    gap, w, h = 10, 854 + 10 + CAM_W * SCALE, 480
    for k in range(start, end):
        game = Image.open(os.path.join(fdir, f"frame_{k:06d}.ppm"))
        canvas = Image.new("RGB", (w, h), (16, 16, 16))
        canvas.paste(game, (0, 0))
        panel = panels.get(k + 1)
        if panel is not None:
            canvas.paste(Image.fromarray(panel),
                         (854 + gap, (h - CAM_H * SCALE) // 2))
        canvas.save(os.path.join(cdir, f"c_{k:06d}.png"))
    return cdir


def encode(cdir, mp4, fps=FPS):
    subprocess.run(
        ["ffmpeg", "-y", "-framerate", str(fps), "-pattern_type", "glob",
         "-i", os.path.join(cdir, "*.png"),
         "-c:v", "libx264", "-pix_fmt", "yuv420p", mp4],
        check=True, capture_output=True)


def main():
    marks = json.load(open(os.path.join(OUT, "env0_marks.json")))
    env0 = np.load(os.path.join(OUT, "ep_env0.npy"))
    early, mid, late = pick_episodes(env0)
    print(f"episodes: early {early} ({env0[early]:+.2f})  "
          f"mid {mid} ({env0[mid]:+.2f})  late {late} ({env0[late]:+.2f})")

    clips = []
    for ep, tag in ((early, "early"), (mid, "mid"), (late, "late")):
        _, start, end = marks["marks"][ep]
        fdir = game_frames(marks, start, end, tag)
        panels = cam_frames(marks, start, end)
        cdir = compose(fdir, panels, start, end, tag)
        mp4 = os.path.join(OUT, f"{tag}.mp4")
        encode(cdir, mp4)
        print(f"{tag}: episode {ep} ticks {start}..{end - 1}, "
              f"{len(panels)} cam frames")
        clips.append((tag, ep, float(env0[ep]), mp4))

    final = os.path.join(OUT, "look_at_tree_learning.mp4")
    labels = [f"{tag} ep {ep}  align {r:+.2f}" for tag, ep, r, _ in clips]
    fc = "".join(
        f"[{i}]drawtext=text='{labels[i]}':x=12:y=12:fontsize=28:"
        f"fontcolor=white:borderw=2[v{i}];" for i in range(3))
    fc += "[v0][v1][v2]concat=n=3:v=1:a=0"
    subprocess.run(
        ["ffmpeg", "-y"] + sum([["-i", c[3]] for c in clips], []) +
        ["-filter_complex", fc, "-c:v", "libx264", "-pix_fmt", "yuv420p",
         final],
        check=True, capture_output=True)
    print("final:", final)


if __name__ == "__main__":
    main()
