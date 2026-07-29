#!/usr/bin/env python3
"""Measure edge vs interior MAE on terrain crop. Open discovery row.

PASS criterion (provisional): edge_mae / interior_mae is reported;
fails only if candidate/golden missing. Agents may tighten threshold later.
"""
from pathlib import Path
from _common import parse_args, load_board, pack_paths, finish


def main():
    args = parse_args()
    board = load_board(args.board)
    p = pack_paths(board, Path(args.repo))
    cand = Path("/tmp/hard_scene_seed0/cand_default.png")
    if not p["golden"].is_file() or not cand.is_file():
        finish(False, reason="missing golden or candidate")
    import numpy as np
    from PIL import Image

    g = np.asarray(Image.open(p["golden"]).convert("RGB"), dtype=np.float32)
    c = np.asarray(Image.open(cand).convert("RGB"), dtype=np.float32)
    # crop
    r0, r1, c0, c1 = 180, 480, 180, 675
    ge, ce = g[r0:r1, c0:c1], c[r0:r1, c0:c1]
    err = np.mean(np.abs(ge - ce), axis=2)
    x = ge.mean(2)
    gx = np.zeros_like(x)
    gy = np.zeros_like(x)
    gx[:, 1:-1] = np.abs(x[:, 2:] - x[:, :-2])
    gy[1:-1, :] = np.abs(x[2:, :] - x[:-2, :])
    edge = (gx + gy) > 40
    e_mae = float(err[edge].mean()) if edge.any() else 0.0
    i_mae = float(err[~edge].mean()) if (~edge).any() else 0.0
    ratio = e_mae / i_mae if i_mae > 1e-6 else float("inf")
    # provisional pass: always PASS if measurable (discovery); flag high ratio
    ok = True
    finish(
        ok,
        edge_mae=e_mae,
        interior_mae=i_mae,
        ratio=ratio,
        edge_frac=float(edge.mean()),
        note="discovery row; ratio>1.4 indicates HF edge residual dominates",
    )


if __name__ == "__main__":
    main()
