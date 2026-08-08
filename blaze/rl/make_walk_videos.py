"""Compose the walk-to-tree + break-log learning mp4 (game + agent camera).

walk_break.py restarts env 0 fresh every episode and saves each episode's
full action stream, so every episode replays from tick 0: script-mode replay
for the real render, rl-mode rerun for the semantic camera panel. Picks
early / mid / late episodes off the saved series (late = last success).

Run (anvil): cd magma && uv run --no-project --with numpy,pillow python rl/make_walk_videos.py
"""
import json
import os
import subprocess

import numpy as np

from make_videos import (MAGMA, OUT, COLORS, CAM_W, CAM_H, SCALE,
                         cam_frames, compose, encode)

FPS = 20  # real time
PREFIX = os.environ.get("WB_PREFIX", "wb")  # wb_* or ppo_* artifact family


def load_episodes():
    eps = []
    with open(os.path.join(OUT, f"{PREFIX}_env0_episodes.jsonl")) as f:
        for line in f:
            eps.append(json.loads(line))
    return eps


def pick(eps):
    early = 0
    succ = [e["ep"] for e in eps if e["success"]]
    late = succ[-1] if succ else len(eps) - 1
    third = len(eps) // 3
    mid_succ = [i for i in succ if third <= i < 2 * third]
    mid = mid_succ[-1] if mid_succ else len(eps) // 2
    if mid in (early, late):
        mid = len(eps) // 2
    return early, mid, late


def write_script(ep, tag):
    acts = ep["actions"]
    script = os.path.join(OUT, f"{PREFIX}_script_{tag}.jsonl")
    with open(script, "w") as f:
        for t, a in enumerate(acts):
            ev = {"tick": t, "type": "action"}
            ev.update(a)
            f.write(json.dumps(ev) + "\n")
    return script, len(acts)


def game_frames(script, ticks, tag, seed):
    fdir = os.path.join(OUT, f"{PREFIX}_frames_{tag}")
    os.makedirs(fdir, exist_ok=True)
    cmd = [os.path.join(MAGMA, "magma_game"),
           "--headless", "--world", "default", "--seed", str(seed),
           "--ticks", str(ticks), "--width", "854", "--height", "480",
           "--script", script,
           "--state-out", os.path.join(OUT, f"{PREFIX}_state_{tag}.jsonl"),
           "--mobs", "off", "--view-distance", "6",
           "--frames-out", fdir, "--frame-offset", "1"]
    r = subprocess.run(cmd, cwd=MAGMA, capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError(f"replay {tag} rc={r.returncode}:\n{r.stdout}{r.stderr}")
    return fdir


def main():
    eps = load_episodes()
    early, mid, late = pick(eps)
    print(f"episodes: early {early} (succ {eps[early]['success']})  "
          f"mid {mid} ({eps[mid]['success']})  late {late} "
          f"({eps[late]['success']})")

    marks = {"seed": 0}
    clips = []
    for idx, tag in ((early, "early"), (mid, "mid"), (late, "late")):
        ep = eps[idx]
        script, ticks = write_script(ep, tag)
        fdir = game_frames(script, ticks, tag, marks["seed"])
        # cam panel: rl rerun of the same actions; pair frame k <-> obs t=k+1
        marks["marks"] = None
        panels = cam_frames_from_actions(ep["actions"], marks["seed"], ticks)
        cdir = compose(fdir, panels, 1, ticks, f"{PREFIX}_{tag}")
        mp4 = os.path.join(OUT, f"{PREFIX}_{tag}.mp4")
        encode(cdir, mp4, fps=FPS)
        print(f"{tag}: episode {idx} {ticks} ticks success={ep['success']}")
        clips.append((tag, idx, ep["success"], mp4))

    final = os.path.join(OUT, "walk_break_learning.mp4"
                     if PREFIX == "wb" else
                     f"{PREFIX}_break_learning.mp4")
    labels = [f"{tag} ep {i}  {'BREAK' if s else 'no break'}"
              for tag, i, s, _ in clips]
    fc = "".join(
        f"[{k}]drawtext=text='{labels[k]}':x=12:y=12:fontsize=28:"
        f"fontcolor=white:borderw=2[v{k}];" for k in range(3))
    fc += "[v0][v1][v2]concat=n=3:v=1:a=0"
    subprocess.run(
        ["ffmpeg", "-y"] + sum([["-i", c[3]] for c in clips], []) +
        ["-filter_complex", fc, "-c:v", "libx264", "-pix_fmt", "yuv420p",
         final],
        check=True, capture_output=True)
    print("final:", final)


def cam_frames_from_actions(actions, seed, end):
    from PIL import Image  # noqa: F401 (parity with make_videos deps)
    proc = subprocess.Popen(
        [os.path.join(MAGMA, "magma_game"), "--rl", "--render", "off",
         "--pace", "unlimited", "--seed", str(seed), "--mobs", "off"],
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
        if not (2 <= t <= end):
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
    for act in actions:
        proc.stdin.write(json.dumps(act) + "\n")
        proc.stdin.flush()
        obs = read_obs()
        keep(obs)
    proc.stdin.close()
    proc.wait(timeout=10)
    return panels


if __name__ == "__main__":
    main()
