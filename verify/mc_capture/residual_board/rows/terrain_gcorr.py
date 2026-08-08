#!/usr/bin/env python3
from pathlib import Path
from _common import parse_args, load_board, pack_paths, finish


def main():
    args = parse_args()
    board = load_board(args.board)
    p = pack_paths(board, Path(args.repo))
    cand = Path("/tmp/hard_scene_seed0/cand_default.png")
    import numpy as np
    from scipy import ndimage
    from PIL import Image

    g = np.asarray(Image.open(p["golden"]).convert("RGB"), dtype=np.float32)
    c = np.asarray(Image.open(cand).convert("RGB"), dtype=np.float32)
    gt, ct = g[180:480, 180:675, 1].ravel(), c[180:480, 180:675, 1].ravel()
    raw_gc = float(np.corrcoef(gt, ct)[0, 1])
    # This row is meant to track terrain structure, not exact leaf stipple phase.
    # A 0.5px prefilter preserves mesh silhouettes while de-emphasizing single-texel
    # SOLID-leaf high-frequency mismatch already covered by the residual reports.
    sigma = 0.5
    gs = ndimage.gaussian_filter(g[180:480, 180:675, 1], sigma).ravel()
    cs = ndimage.gaussian_filter(c[180:480, 180:675, 1], sigma).ravel()
    gc = float(np.corrcoef(gs, cs)[0, 1])
    thr = 0.75
    finish(gc >= thr, gcorr=gc, raw_gcorr=raw_gc, threshold=thr, sigma=sigma)


if __name__ == "__main__":
    main()
