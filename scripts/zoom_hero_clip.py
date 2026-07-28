"""Render the zoom-video hero clip: trained-chain agent POV, exact renderer.

Replays rl/out/chain_actions_s10.json through magma --rl and captures a
16:9 window of game frames around the first tree-chop (the visually
liveliest early-chain stretch), sized to drop straight into the zoom
mosaic overlay.

Run: cd netherite/c/magma && uv run --no-project --with numpy python \
       ../../scripts/zoom_hero_clip.py
"""
import json
import os
import shutil
import subprocess
import sys

MAGMA = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                     "c", "magma")
OUT = os.path.join(MAGMA, "rl", "out")
HERO_DIR = "/home/infatoshi/dev/nw/.tmp/zoom_hero"
SEED = 10
HW, HH = 1708, 960          # 16:9, 2x the mosaic tile aspect content
WINDOW = 400                # frames captured


def replay(width, height, fdir, frame_offset, frame_every=1):
    acts = json.load(open(os.path.join(OUT, f"chain_actions_s{SEED}.json")))
    shutil.rmtree(fdir, ignore_errors=True)
    os.makedirs(fdir)
    proc = subprocess.Popen(
        [os.path.join(MAGMA, "magma_game"), "--rl", "--render", "off",
         "--pace", "unlimited", "--seed", str(SEED), "--mobs", "off",
         "--width", str(width), "--height", str(height),
         "--frames-out", fdir, "--frame-offset", str(frame_offset),
         "--frame-every", str(frame_every)],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL, text=True, bufsize=1, cwd=MAGMA)

    def read_obs():
        while True:
            ln = proc.stdout.readline()
            if not ln:
                raise RuntimeError("rl replay died")
            if ln.startswith("{"):
                return json.loads(ln)

    first_log = None
    obs = read_obs()
    for i, a in enumerate(acts):
        proc.stdin.write(json.dumps(a) + "\n")
        proc.stdin.flush()
        obs = read_obs()
        if first_log is None and obs.get("inv_counts", [0])[0] > 0:
            first_log = obs["t"]
        # enough frames past the window? stop feeding
        if first_log is not None and obs["t"] > frame_offset + WINDOW + 10:
            break
    proc.stdin.close()
    proc.terminate()
    return first_log


def main():
    # pass 1 (cheap, tiny frames): find the first-log tick
    probe_dir = os.path.join(HERO_DIR, "probe")
    first_log = replay(64, 36, probe_dir, frame_offset=10 ** 9)
    if first_log is None:
        sys.exit("chain replay never chopped a log?")
    start = max(first_log - 140, 0)
    print(f"first log at t={first_log}; hero window [{start}, {start + WINDOW})")

    # pass 2: capture the real window at hero resolution
    frames_dir = os.path.join(HERO_DIR, "frames")
    replay(HW, HH, frames_dir, frame_offset=start)
    n = len([f for f in os.listdir(frames_dir) if f.endswith(".ppm")])
    print(f"captured {n} hero frames at {HW}x{HH} in {frames_dir}")
    with open(os.path.join(HERO_DIR, "META.json"), "w") as f:
        json.dump({"start": start, "first_log": first_log,
                   "w": HW, "h": HH, "frames": n}, f)


if __name__ == "__main__":
    main()
