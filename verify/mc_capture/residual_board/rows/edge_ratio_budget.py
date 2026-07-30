#!/usr/bin/env python3
from pathlib import Path
from _common import parse_args, load_board, pack_paths, finish


def main():
    args = parse_args()
    board = load_board(args.board)
    p = pack_paths(board, Path(args.repo))
    cand = Path("/tmp/hard_scene_seed0/cand_default.png")
    import numpy as np
    from PIL import Image

    g = np.asarray(Image.open(p["golden"]).convert("RGB"), dtype=np.float32)
    c = np.asarray(Image.open(cand).convert("RGB"), dtype=np.float32)
    ge, ce = g[180:480, 180:675], c[180:480, 180:675]
    err = np.mean(np.abs(ge - ce), axis=2)
    x = ge.mean(2)
    gx = np.zeros_like(x)
    gy = np.zeros_like(x)
    gx[:, 1:-1] = np.abs(x[:, 2:] - x[:, :-2])
    gy[1:-1, :] = np.abs(x[2:, :] - x[:-2, :])
    edge = (gx + gy) > 40
    e_mae = float(err[edge].mean())
    i_mae = float(err[~edge].mean())
    ratio = e_mae / i_mae if i_mae > 1e-6 else 999.0
    thr = 1.25
    finish(ratio <= thr, edge_mae=e_mae, interior_mae=i_mae, ratio=ratio, threshold=thr)


if __name__ == "__main__":
    main()
