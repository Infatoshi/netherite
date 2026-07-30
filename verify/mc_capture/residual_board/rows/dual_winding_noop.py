#!/usr/bin/env python3
"""dual_winding_noop — solid-path factor: dual winding is irrelevant on hard-scene.

Java GL draws both windings with cull disabled; game_candidate historically
submitted each tri twice so our backface cull kept one. HF ablations already
showed crop delta ~0.001 (dual vs MAGMA_NO_DUAL_WIND). This row locks that:

  1) game_candidate defaults dual OFF (opt-in MAGMA_DUAL_WIND=1 only)
  2) if both frames exist, |crop_dual - crop_nodual| < 0.1
  3) default-path terrain crop still <= 15 (closed integration gate)
"""
from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

from _common import parse_args, load_board, pack_paths, finish


def _crop_mean(repo: Path, golden: Path, cand: Path, crop: str) -> float | None:
    diff = repo / "java/render-opt/wholeframe/diff_frame.py"
    if not cand.is_file() or not golden.is_file():
        return None
    r = subprocess.run(
        [sys.executable, str(diff), str(golden), str(cand), "--crop", crop],
        cwd=str(repo),
        capture_output=True,
        text=True,
    )
    out = (r.stdout or "") + (r.stderr or "")
    m = re.search(r"crop\s*:\s*.*?mean=\s*([0-9.]+)", out)
    return float(m.group(1)) if m else None


def _default_dual_off(gc_src: str) -> bool:
    """Default dual must be off; only MAGMA_DUAL_WIND=1 enables it."""
    if "MAGMA_DUAL_WIND" not in gc_src:
        return False
    # dual starts 0, then env may set 1
    if re.search(r"\bint\s+dual\s*=\s*0\s*;", gc_src):
        return True
    # accept dual=1 only if gated inverted from NO_DUAL (legacy) — fail that
    return False


def main():
    args = parse_args()
    board = load_board(args.board)
    repo = Path(args.repo)
    p = pack_paths(board, repo)
    crop = board["capture_pack"]["terrain_crop"]

    gc_path = repo / "verify/mc_capture/game_candidate.c"
    gc_src = gc_path.read_text() if gc_path.is_file() else ""
    dual_off = _default_dual_off(gc_src)

    cand_default = Path("/tmp/hard_scene_seed0/cand_default.png")
    cand_dual = Path("/tmp/hard_scene_seed0/cand_dual.png")
    cand_nodual = Path("/tmp/hard_scene_seed0/cand_nodual.png")

    # Prefer explicit dual/nodual pair; fall back to default as the dual-off path.
    mean_default = _crop_mean(repo, p["golden"], cand_default, crop)
    mean_dual = _crop_mean(repo, p["golden"], cand_dual, crop)
    mean_nodual = _crop_mean(repo, p["golden"], cand_nodual, crop)

    # If only default exists and default dual is off, treat it as nodual baseline.
    if mean_nodual is None and dual_off and mean_default is not None:
        mean_nodual = mean_default

    crop_delta = None
    if mean_dual is not None and mean_nodual is not None:
        crop_delta = abs(mean_dual - mean_nodual)

    delta_ok = crop_delta is None or crop_delta < 0.1
    gate_ok = mean_default is not None and mean_default <= 15.0

    ok = bool(dual_off and delta_ok and gate_ok)
    finish(
        ok,
        default_dual_off=dual_off,
        terrain_crop_mean=mean_default,
        crop_dual=mean_dual,
        crop_nodual=mean_nodual,
        crop_delta=crop_delta,
        delta_threshold=0.1,
        gate=15.0,
        note="dual winding must be opt-in; crop dual vs nodual delta < 0.1",
    )


if __name__ == "__main__":
    main()
