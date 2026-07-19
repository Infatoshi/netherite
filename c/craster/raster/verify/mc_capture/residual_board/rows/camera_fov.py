#!/usr/bin/env python3
import json
from pathlib import Path
from _common import parse_args, load_board, pack_paths, finish


def main():
    args = parse_args()
    board = load_board(args.board)
    p = pack_paths(board, Path(args.repo))
    cam = json.loads(p["camera"].read_text())
    fe = float(cam.get("fov_effective", 0))
    flying = bool(cam.get("is_flying"))
    ok = flying and 76.5 <= fe <= 77.5
    finish(
        ok,
        fov_setting=cam.get("fov_setting"),
        fov_modifier=cam.get("fov_modifier"),
        fov_effective=fe,
        is_flying=flying,
    )


if __name__ == "__main__":
    main()
