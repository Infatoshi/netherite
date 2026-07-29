#!/usr/bin/env python3
import json
from pathlib import Path
from _common import parse_args, load_board, pack_paths, finish


def main():
    args = parse_args()
    board = load_board(args.board)
    p = pack_paths(board, Path(args.repo))
    cam = json.loads(p["camera"].read_text())
    w = cam.get("world") or {}
    seed = w.get("seed")
    folder = w.get("save_folder")
    ok = seed == 0 and folder == "qrl_0"
    finish(ok, seed=seed, save_folder=folder, fingerprint=w.get("fingerprint"))


if __name__ == "__main__":
    main()
