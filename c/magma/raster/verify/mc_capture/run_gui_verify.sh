#!/usr/bin/env bash
# run_gui_verify.sh - the container-screen pixel gate: render each GUI screen
# (crafting table / furnace / player inventory) through the real gm_screen_draw
# path (gui_candidate.c) and pixel-diff the 176x166 panel region against the
# REAL Minecraft goldens captured by capture_gui.sh (mc_gui_*_a.png).
#
# Gate design (PRODUCT.md "Visual acceptance"):
#   - The compared region is the panel rect INSET BY 4px per side. The inset
#     drops only the panel's rounded corners / 1px border edge, where the
#     translucent corners composite over the live 3D scene (which magma's
#     candidate legitimately does not reproduce for this 2D gate).
#   - Tolerance is CALIBRATED, not guessed: the Java-vs-Java repeat captures
#     (_a vs _b) give the measured noise floor per screen; the gate is
#     noise_floor + MARGIN (default 1.0 mean abs/channel).
#   - HARD gates: table + furnace (fail -> exit 1).
#   - INFORMATIONAL: the player inventory screen. Its panel contains the live
#     3D player-model preview, which magma does not render (documented gap
#     in PRODUCT.md). Its numbers are printed, not gated, so the gap is
#     visible instead of masked.
set -euo pipefail
cd "$(dirname "$0")/../../.."          # -> c/magma

MCSIM="$(cd ../mc-sim/core && pwd)"
OUT=raster/verify/mc_capture
FLAGS=(-O2 -ffp-contract=off -Wall -Wextra -I. -Icore -I"$MCSIM")
MARGIN="${MARGIN:-1.0}"

echo "== build gui_candidate =="
make -s game/screen.o game/hud.o game/item_render.o game/container_live.o game/runtime.o game/config.o \
    game/player_ctl.o game/world_live.o game/live_sim.o game/mob_live.o \
    game/dragon_live.o game/structures_live.o game/portal_live.o game/furnace_live.o \
    game/caps.o world/light.o world/mesh_mc.o world/populate_mc.o world/blocks.o \
    world/mesh.o world/world.o renderkernels/rk_31_facebakery_make_quad.o \
    assets/blockmodels.o core/math.o core/shade.o
gcc "${FLAGS[@]}" "$OUT/gui_candidate.c" \
    game/screen.o game/hud.o game/item_render.o game/runtime.o game/config.o game/player_ctl.o \
    game/world_live.o game/live_sim.o game/mob_live.o game/dragon_live.o \
    game/structures_live.o game/portal_live.o game/furnace_live.o \
    game/container_live.o game/caps.o world/light.o world/mesh_mc.o \
    world/populate_mc.o world/blocks.o world/mesh.o world/world.o \
    renderkernels/rk_31_facebakery_make_quad.o assets/blockmodels.o \
    core/math.o core/shade.o -lm -o "$OUT/gui_candidate"

echo "== render magma screens =="
# capture_gui.sh grabs the 854x480 window; container ids: 0 player, 1 table, 2 furnace
"$OUT/gui_candidate" --container 1 --w 854 --h 480 --ppm "$OUT/magma_gui_table.ppm"
"$OUT/gui_candidate" --container 2 --w 854 --h 480 --ppm "$OUT/magma_gui_furnace.ppm"
"$OUT/gui_candidate" --container 0 --w 854 --h 480 --ppm "$OUT/magma_gui_inventory.ppm"

echo "== panel-region pixel diff (inset 4px; tolerance = repeat-capture noise + $MARGIN) =="
rc=0
uv run --no-project --with pillow --with numpy python - "$OUT" "$MARGIN" <<'PY' || rc=$?
import sys
import numpy as np
from PIL import Image

out, margin = sys.argv[1], float(sys.argv[2])
INSET = 4

def panel_crop(img):
    """176x166*scale panel rect at the vanilla GuiContainer origin (integer
    division in GUI units: floor((scaledW-176)/2)), inset by INSET gui px."""
    a = np.asarray(img.convert("RGB")).astype(np.int16)
    h, w = a.shape[:2]
    s = max(1, h // 240)
    gw, gh = -(-w // s), -(-h // s)
    pw, ph = 176 * s, 166 * s
    x0, y0 = (gw - 176) // 2 * s, (gh - 166) // 2 * s
    i = INSET * s
    return a[y0 + i:y0 + ph - i, x0 + i:x0 + pw - i]

def mean_abs(a, b):
    return float(np.abs(a - b).mean())

fail = 0
print(f"{'screen':<10} {'noise(J-vs-J)':>13} {'magma-vs-J':>13} {'gate':>8}  verdict")
for name, hard in (("table", True), ("furnace", True), ("inventory", False)):
    ja = panel_crop(Image.open(f"{out}/mc_gui_{name}_a.png"))
    jb = panel_crop(Image.open(f"{out}/mc_gui_{name}_b.png"))
    c  = panel_crop(Image.open(f"{out}/magma_gui_{name}.ppm"))
    assert ja.shape == c.shape, (name, ja.shape, c.shape)
    noise = mean_abs(ja, jb)
    diff  = mean_abs(ja, c)
    gate  = noise + margin
    if hard:
        ok = diff <= gate
        verdict = "PASS" if ok else "FAIL"
        fail |= not ok
    else:
        verdict = "INFO (player-model preview not rendered; not gated)"
    print(f"{name:<10} {noise:>13.3f} {diff:>13.3f} {gate:>8.3f}  {verdict}")
sys.exit(1 if fail else 0)
PY
if [ "$rc" -eq 0 ]; then echo "gui verify: PASS"; else echo "gui verify: FAIL"; fi
exit "$rc"
