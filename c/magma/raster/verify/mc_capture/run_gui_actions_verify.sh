#!/usr/bin/env bash
# Render magma's deterministic inventory action sequence and pixel-diff each
# visible state against capture_gui_actions.sh's real-Minecraft PNG golden.
set -euo pipefail
cd "$(dirname "$0")/../../.."          # -> c/magma

MCSIM="$(cd ../mc-sim/core && pwd)"
OUT=raster/verify/mc_capture
FLAGS=(-O2 -ffp-contract=off -Wall -Wextra -I. -Icore -I"$MCSIM")
MARGIN="${MARGIN:-1.0}"

echo "== build gui_actions_candidate =="
make -s game/screen.o game/player_preview.o game/hud.o game/item_render.o game/container_live.o game/runtime.o \
    game/fluid_live.o game/config.o game/player_ctl.o game/sel_box.o game/world_live.o \
    game/live_sim.o game/mob_live.o game/dragon_live.o game/structures_live.o \
    game/portal_live.o game/furnace_live.o game/caps.o world/light.o world/mesh_mc.o \
    world/populate_mc.o world/blocks.o world/mesh.o world/world.o \
    renderkernels/rk_31_facebakery_make_quad.o assets/blockmodels.o core/math.o core/shade.o cpu/raster_cpu.o
gcc "${FLAGS[@]}" "$OUT/gui_actions_candidate.c" \
    game/screen.o game/player_preview.o game/hud.o game/item_render.o game/runtime.o game/fluid_live.o \
    game/config.o game/player_ctl.o game/sel_box.o game/world_live.o game/live_sim.o \
    game/mob_live.o game/dragon_live.o game/structures_live.o game/portal_live.o \
    game/furnace_live.o game/container_live.o game/caps.o world/light.o \
    world/mesh_mc.o world/populate_mc.o world/blocks.o world/mesh.o world/world.o \
    renderkernels/rk_31_facebakery_make_quad.o assets/blockmodels.o \
    core/math.o core/shade.o cpu/raster_cpu.o -lm -o "$OUT/gui_actions_candidate"

echo "== render magma action states =="
"$OUT/gui_actions_candidate" "$OUT"

echo "== action pixel diff (panel + slot/cursor ROIs; tolerance = repeat noise + $MARGIN) =="
uv run --no-project --with pillow --with numpy python - "$OUT" "$MARGIN" <<'PY'
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image

out = Path(sys.argv[1])
margin = float(sys.argv[2])
steps = [
    "00_initial",
    "01_pickup_a",
    "02_place_b",
    "03_split_b",
    "04_deposit_one_c",
    "05_shift_b_to_hotbar",
    "06_swap_hotbar_0_1",
    "07_drop_one_hotbar0",
]
held_cursor = {"01_pickup_a", "03_split_b"}
centers = [(282, 258), (318, 258), (354, 258), (282, 374), (318, 374)]
hovered = {
    "00_initial": (282, 258),
    "01_pickup_a": (282, 258),
    "02_place_b": (318, 258),
    "03_split_b": (318, 258),
    "04_deposit_one_c": (354, 258),
    "05_shift_b_to_hotbar": (318, 258),
    "06_swap_hotbar_0_1": (282, 374),
    "07_drop_one_hotbar0": (282, 374),
}


def rgb(path):
    return np.asarray(Image.open(path).convert("RGB"), dtype=np.int16)


def panel_mask(shape):
    h, w = shape[:2]
    s = max(1, h // 240)
    gw, gh = -(-w // s), -(-h // s)
    x0, y0 = (gw - 176) // 2 * s, (gh - 166) // 2 * s
    mask = np.zeros((h, w), dtype=bool)
    i = 4 * s
    mask[y0 + i:y0 + 166 * s - i, x0 + i:x0 + 176 * s - i] = True
    # GuiInventory's live 3D player preview is a documented magma gap. Keep
    # armor/craft/main/hotbar pixels gated; mask only the preview viewport.
    mask[y0 + 7 * s:y0 + 79 * s, x0 + 24 * s:x0 + 76 * s] = False
    return mask


def mean_abs(a, b, mask=None):
    d = np.abs(a - b)
    return float(d[mask].mean() if mask is not None else d.mean())


ja = rgb(out / "mc_gui_inventory_a.png")
jb = rgb(out / "mc_gui_inventory_b.png")
mask = panel_mask(ja.shape)
panel_noise = mean_abs(ja, jb, mask)
panel_gate = panel_noise + margin
roi_noise = max(
    mean_abs(ja[y - 22:y + 22, x - 22:x + 22],
             jb[y - 22:y + 22, x - 22:x + 22])
    for x, y in centers
)
roi_gate = roi_noise + margin
failed = False

print(f"{'step':<27} {'panel':>8} {'slot/cursor max':>15} {'gate':>8}  verdict")
for step in steps:
    oracle_path = out / f"mc_gui_action_{step}.png"
    magma_path = out / f"magma_gui_action_{step}.ppm"
    oracle, magma = rgb(oracle_path), rgb(magma_path)
    if oracle.shape != magma.shape or oracle.shape != ja.shape:
        raise SystemExit(f"{step}: shape mismatch {oracle.shape} {magma.shape} {ja.shape}")
    panel = mean_abs(oracle, magma, mask)
    rois = []
    for x, y in centers:
        om = oracle[y - 22:y + 22, x - 22:x + 22]
        mm = magma[y - 22:y + 22, x - 22:x + 22]
        # Magma's windowed client draws a bare white cursor cross. The oracle
        # FBO does not contain the OS pointer, so ignore only that 12x12 center
        # when the cursor is empty. Held-stack frames remain entirely gated.
        if step not in held_cursor:
            keep = np.ones(om.shape[:2], dtype=bool)
            if (x, y) == hovered[step]:
                keep[16:28, 16:28] = False
            rois.append(mean_abs(om, mm, keep))
        else:
            rois.append(mean_abs(om, mm))
    worst = max(rois)
    ok = panel <= panel_gate and worst <= roi_gate
    verdict = "PASS" if ok else "FAIL"
    print(f"{step:<27} {panel:>8.3f} {worst:>15.3f} {max(panel_gate, roi_gate):>8.3f}  {verdict}")
    if not ok:
        failed = True
        diff = np.abs(oracle - magma).clip(0, 255).astype(np.uint8)
        diff_path = out / f"diff_gui_action_{step}.png"
        Image.fromarray(diff).save(diff_path)
        print(f"  diff: {diff_path}")

meta = json.load(open(out / "gui_actions_scene.json"))
close_path = out / "mc_gui_action_08_close.png"
close_ok = close_path.exists() and Image.open(close_path).size == (854, 480)
close_ok &= meta.get("close_focusdiag", {}).get("screen") is None
print(f"{'08_close':<27} {'--':>8} {'--':>15} {'--':>8}  {'PASS' if close_ok else 'FAIL'} (screen state)")
failed |= not close_ok
raise SystemExit(1 if failed else 0)
PY
