#!/usr/bin/env python3
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
    cand = Path("/tmp/hard_scene_seed0/cand_default.png")
    diff = repo / "java/render-opt/wholeframe/diff_frame.py"
    r = subprocess.run(
        [
            sys.executable,
            str(diff),
            str(p["golden"]),
            str(cand),
            "--crop",
            board["capture_pack"]["terrain_crop"],
        ],
        capture_output=True,
        text=True,
    )
    m = re.search(r"crop\s*:\s*.*?mean=\s*([0-9.]+)", r.stdout + r.stderr)
    if not m:
        finish(False, reason="parse fail")
    mean = float(m.group(1))
    thr = 10.0
    finish(mean <= thr, terrain_crop_mean=mean, threshold=thr)


if __name__ == "__main__":
    main()
