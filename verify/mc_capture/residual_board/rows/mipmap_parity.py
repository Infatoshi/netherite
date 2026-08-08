#!/usr/bin/env python3
import re
from pathlib import Path
from _common import parse_args, load_board, pack_paths, finish


def main():
    args = parse_args()
    board = load_board(args.board)
    repo = Path(args.repo)
    p = pack_paths(board, repo)
    opt = p["options"].read_text() if p["options"].is_file() else ""
    m = re.search(r"mipmapLevels:(\d+)", opt)
    mips = int(m.group(1)) if m else -1
    bm = (repo / "magma/assets/blockmodels.c").read_text()
    leaf_solid = "LEAF(spr)" in bm and "CR_LAYER_SOLID" in bm
    # game_candidate solid use_mips=0
    gc = (repo / "verify/mc_capture/game_candidate.c").read_text()
    solid_no_mip = "CR_LAYER_SOLID" in gc and re.search(
        r"sh_solid\s*=\s*\{[^}]*CR_LAYER_SOLID", gc, re.S
    )
    # simpler: solid shade ctx has use_mips field position - check line
    ok_solid = "CR_LAYER_SOLID, 0, 0, 0.f" in gc or re.search(
        r"sh_solid.*?0,\s*fon,\s*\n\s*CR_LAYER_SOLID,\s*0,\s*0", gc
    )
    ok = mips == 0 and leaf_solid and bool(ok_solid)
    finish(
        ok,
        java_mipmapLevels=mips,
        leaf_macro_solid=leaf_solid,
        solid_use_mips0=bool(ok_solid),
    )


if __name__ == "__main__":
    main()
