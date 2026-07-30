#!/usr/bin/env python3
import json
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
    if not cand.is_file():
        finish(False, reason=f"missing candidate {cand}")
    script = repo / "verify/mc_capture/subpixel_reg.py"
    out = Path("/tmp/hard_scene_agents/board_subpixel.json")
    r = subprocess.run(
        [
            sys.executable,
            str(script),
            "--golden",
            str(p["golden"]),
            "--cand",
            str(cand),
            "--crop",
            board["capture_pack"]["terrain_crop"],
            "--out",
            str(out),
        ],
        cwd=str(repo),
        capture_output=True,
        text=True,
    )
    if not out.is_file():
        finish(False, reason="subpixel script failed", stderr=r.stderr[-500:])
    d = json.loads(out.read_text())
    # Closed row: PASS means registration is NOT the residual (drop < 2)
    ok = float(d.get("mae_drop", 99)) < 2.0
    finish(ok, **d)


if __name__ == "__main__":
    main()
