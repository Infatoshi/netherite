#!/usr/bin/env python3
from pathlib import Path
from _common import parse_args, finish


def main():
    args = parse_args()
    repo = Path(args.repo)
    # Java: disableAlpha before SOLID
    er = (
        repo
        / "java/oracle-src/net/minecraft/client/renderer/EntityRenderer.java"
    ).read_text()
    java_ok = "disableAlpha" in er and "BlockRenderLayer.SOLID" in er
    # C: SOLID forces alpha 255, alpha_test not default for SOLID
    shade = (repo / "magma/core/shade.c").read_text()
    c_ok = "CR_LAYER_SOLID" in shade and "force full alpha" in shade
    # default SOLID_ALPHA off
    solid_alpha_default_off = 'MAGMA_SOLID_ALPHA' not in shade or (
        "sa = (s && atoi(s) != 0) ? 1 : 0" in shade
    )
    finish(
        bool(java_ok and c_ok),
        java_disableAlpha_before_solid=java_ok,
        c_solid_force_opaque=c_ok,
        solid_alpha_opt_in_only=solid_alpha_default_off,
    )


if __name__ == "__main__":
    main()
