#!/usr/bin/env python3
"""Integration gate: terrain crop MAE. Uses existing candidate if fresh enough, else rebuilds."""
import json
import re
import subprocess
import sys
from pathlib import Path
from _common import parse_args, load_board, pack_paths, finish


def main():
    args = parse_args()
    board = load_board(args.board)
    repo = Path(args.repo)
    p = pack_paths(board, repo)
    pose = board["capture_pack"]["pose"]
    cand = Path("/tmp/hard_scene_seed0/cand_default.png")
    if not cand.is_file():
        finish(False, reason="no candidate png; build game_candidate first")

    diff = repo / "java/render-opt/wholeframe/diff_frame.py"
    crop = board["capture_pack"]["terrain_crop"]
    r = subprocess.run(
        [
            sys.executable,
            str(diff),
            str(p["golden"]),
            str(cand),
            "--crop",
            crop,
        ],
        cwd=str(repo),
        capture_output=True,
        text=True,
    )
    out = r.stdout + r.stderr
    # parse "crop : ... mean= 12.989"
    m = re.search(r"crop\s*:\s*.*?mean=\s*([0-9.]+)", out)
    if not m:
        finish(False, reason="could not parse crop mean", log=out[-800:])
    mean = float(m.group(1))
    gate = 15.0
    ok = mean <= gate
    finish(
        ok,
        terrain_crop_mean=mean,
        gate=gate,
        golden=str(p["golden"]),
        cand=str(cand),
        pose=pose,
    )


if __name__ == "__main__":
    main()
