"""Render the zoom-video hero clip: trained-chain agent POV, exact renderer.

Replays rl/out/chain_actions_s10.json through magma --rl, scores every
sliding window of the run by camera motion (yaw/pitch turn + travel, with a
hard penalty for >1.5s static stares that read as a frozen video), and
captures the liveliest window at hero resolution.

Run: cd netherite/c/magma && uv run --no-project --with numpy python \
       ../../scripts/zoom_hero_clip.py
"""
import json
import math
import os
import shutil
import subprocess

MAGMA = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                     "c", "magma")
OUT = os.path.join(MAGMA, "rl", "out")
HERO_DIR = "/home/infatoshi/dev/nw/.tmp/zoom_hero"
SEED = 10
HW, HH = 1708, 960
WINDOW = 400


def replay(width, height, fdir, frame_offset, stop_after=None):
    acts = json.load(open(os.path.join(OUT, f"chain_actions_s{SEED}.json")))
    shutil.rmtree(fdir, ignore_errors=True)
    os.makedirs(fdir)
    proc = subprocess.Popen(
        [os.path.join(MAGMA, "magma_game"), "--rl", "--render", "off",
         "--pace", "unlimited", "--seed", str(SEED), "--mobs", "off",
         "--width", str(width), "--height", str(height),
         "--frames-out", fdir, "--frame-offset", str(frame_offset),
         "--frame-every", "1"],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL, text=True, bufsize=1, cwd=MAGMA)

    def read_obs():
        while True:
            ln = proc.stdout.readline()
            if not ln:
                raise RuntimeError("rl replay died")
            if ln.startswith("{"):
                return json.loads(ln)

    poses = [read_obs()]
    for a in acts:
        proc.stdin.write(json.dumps(a) + "\n")
        proc.stdin.flush()
        poses.append(read_obs())
        if stop_after is not None and poses[-1]["t"] > stop_after:
            break
    proc.stdin.close()
    proc.terminate()
    return poses


def best_motion_window(poses, win):
    """Start tick of the liveliest, least-freezy camera window."""
    m = []
    for a, b in zip(poses, poses[1:]):
        dyaw = abs((b["yaw"] - a["yaw"] + 180) % 360 - 180)
        dpitch = abs(b["pitch"] - a["pitch"])
        dist = math.hypot(b["x"] - a["x"], b["z"] - a["z"])
        m.append(dyaw + dpitch + 25 * dist)
    best_s, best_v = 0, -1e18
    for s0 in range(0, max(len(m) - win, 1), 10):
        w = m[s0:s0 + win]
        total = sum(w)
        frozen = run = 0
        for v in w:
            run = run + 1 if v < 0.5 else 0
            if run > 30:          # >1.5s static stare
                frozen += 1
        v = total - 40.0 * frozen
        if v > best_v:
            best_s, best_v = s0, v
    return best_s


def main():
    probe_dir = os.path.join(HERO_DIR, "probe")
    poses = replay(64, 36, probe_dir, frame_offset=10 ** 9)
    start = best_motion_window(poses, WINDOW)
    print(f"liveliest window [{start}, {start + WINDOW}) of {len(poses)} ticks")

    frames_dir = os.path.join(HERO_DIR, "frames")
    replay(HW, HH, frames_dir, frame_offset=start,
           stop_after=start + WINDOW + 5)
    n = len([f for f in os.listdir(frames_dir) if f.endswith(".ppm")])
    print(f"captured {n} hero frames at {HW}x{HH} in {frames_dir}")
    with open(os.path.join(HERO_DIR, "META.json"), "w") as f:
        json.dump({"start": start, "w": HW, "h": HH, "frames": n}, f)


if __name__ == "__main__":
    main()
