#!/usr/bin/env bash
# run_gui_verify.sh - the container-screen pixel gate: render each GUI screen
# (crafting table / furnace / player inventory / single chest) through the real
# gm_screen_draw path (gui_candidate.c) and pixel-diff against REAL Minecraft
# goldens from capture_gui.sh (mc_gui_*_a.png).
#
# Gate design (PRODUCT.md "Visual acceptance"):
#   - Panel region is inset by 4px per side (rounded corners over the 3D scene).
#   - Tolerance is CALIBRATED: Java-vs-Java repeat (_a vs _b) + MARGIN (default 1.0).
#   - Inventory: dedicated 104x144 (scale-2) player-preview ROI is a HARD gate
#     (magma-vs-J <= preview noise + PREVIEW_MARGIN). Non-preview panel pixels
#     are gated separately so a good chrome mean cannot dilute a bad model.
#   - Second look-at pose: action golden mc_gui_action_00_initial.png (cursor on
#     inv slot A at fb 282,258) vs magma render at the same mouse. Missing that
#     golden is fail-closed.
#   - Slot/cursor ROIs live in run_gui_actions_verify.sh and do NOT claim preview.
#   - Chest fails clearly if mc_gui_chest_{a,b}.png are absent (no fabricate).
#   - Not implemented: dispenser/dropper/hopper/enchant/brew/anvil/villager/
#     creative/beacon/horse/shulker.
set -euo pipefail
cd "$(dirname "$0")/../../.."          # -> c/magma

MCSIM="$(cd ../mc-sim/core && pwd)"
OUT=raster/verify/mc_capture
FLAGS=(-O2 -ffp-contract=off -Wall -Wextra -I. -Icore -I"$MCSIM")
MARGIN="${MARGIN:-1.0}"
# Software-raster vs GL coverage/filtering residual only — not a pose fudge.
# Calibrated from the exact ModelPlayer path residual above J-vs-J noise on the
# parked-mouse inventory golden (~0.6 mean abs); keep +1.0 headroom like panel.
PREVIEW_MARGIN="${PREVIEW_MARGIN:-1.0}"

echo "== build gui_candidate =="
make -s game/screen.o game/player_preview.o game/hud.o game/item_render.o game/container_live.o game/runtime.o game/fluid_live.o game/config.o \
    game/player_ctl.o game/sel_box.o game/world_live.o game/live_sim.o game/mob_live.o \
    game/dragon_live.o game/structures_live.o game/portal_live.o game/furnace_live.o \
    game/chest_live.o game/caps.o world/light.o world/mesh_mc.o world/populate_mc.o world/blocks.o \
    world/mesh.o world/world.o renderkernels/rk_31_facebakery_make_quad.o \
    assets/blockmodels.o core/math.o core/shade.o cpu/raster_cpu.o
gcc "${FLAGS[@]}" "$OUT/gui_candidate.c" \
    game/screen.o game/player_preview.o game/hud.o game/item_render.o game/runtime.o game/fluid_live.o game/config.o game/player_ctl.o game/sel_box.o \
    game/world_live.o game/live_sim.o game/mob_live.o game/dragon_live.o \
    game/structures_live.o game/portal_live.o game/furnace_live.o game/chest_live.o \
    game/container_live.o game/caps.o world/light.o world/mesh_mc.o \
    world/populate_mc.o world/blocks.o world/mesh.o world/world.o \
    renderkernels/rk_31_facebakery_make_quad.o assets/blockmodels.o \
    core/math.o core/shade.o cpu/raster_cpu.o -lm -o "$OUT/gui_candidate"

echo "== render magma screens =="
# capture_gui.sh grabs the 854x480 window; container ids: 0 player, 1 table, 2 furnace, 3 chest
"$OUT/gui_candidate" --container 1 --w 854 --h 480 --ppm "$OUT/magma_gui_table.ppm"
"$OUT/gui_candidate" --container 2 --w 854 --h 480 --ppm "$OUT/magma_gui_furnace.ppm"
"$OUT/gui_candidate" --container 0 --w 854 --h 480 --ppm "$OUT/magma_gui_inventory.ppm"
"$OUT/gui_candidate" --container 3 --w 854 --h 480 --ppm "$OUT/magma_gui_chest.ppm"

# Second fixed mouse pose for the inventory preview (slot A center = action_00).
# Reuse gui_actions_candidate when present; otherwise a one-off draw via a tiny
# inline is avoided by building the actions candidate (same link set).
echo "== render inventory preview pose2 (mouse on inv slot A) =="
make -s game/screen.o game/player_preview.o game/hud.o game/item_render.o game/container_live.o game/runtime.o \
    game/fluid_live.o game/config.o game/player_ctl.o game/sel_box.o game/world_live.o \
    game/live_sim.o game/mob_live.o game/dragon_live.o game/structures_live.o \
    game/portal_live.o game/furnace_live.o game/chest_live.o game/caps.o world/light.o world/mesh_mc.o \
    world/populate_mc.o world/blocks.o world/mesh.o world/world.o \
    renderkernels/rk_31_facebakery_make_quad.o assets/blockmodels.o core/math.o core/shade.o cpu/raster_cpu.o
gcc "${FLAGS[@]}" "$OUT/gui_actions_candidate.c" \
    game/screen.o game/player_preview.o game/hud.o game/item_render.o game/runtime.o game/fluid_live.o \
    game/config.o game/player_ctl.o game/sel_box.o game/world_live.o game/live_sim.o \
    game/mob_live.o game/dragon_live.o game/structures_live.o game/portal_live.o \
    game/furnace_live.o game/chest_live.o game/container_live.o game/caps.o world/light.o \
    world/mesh_mc.o world/populate_mc.o world/blocks.o world/mesh.o world/world.o \
    renderkernels/rk_31_facebakery_make_quad.o assets/blockmodels.o \
    core/math.o core/shade.o cpu/raster_cpu.o -lm -o "$OUT/gui_actions_candidate"
"$OUT/gui_actions_candidate" "$OUT"
# Pose2 golden is the action_00 oracle (cursor on A); magma file from actions run.
cp -f "$OUT/magma_gui_action_00_initial.ppm" "$OUT/magma_gui_inventory_pose2.ppm"

echo "implemented magma screens: inventory, crafting table, furnace, chest"
echo "not implemented: dispenser/dropper, hopper, enchanting, brewing, anvil, villager, creative, beacon, horse, shulker"

echo "== panel + preview ROI pixel diff =="
rc=0
uv run --no-project --with pillow --with numpy python - "$OUT" "$MARGIN" "$PREVIEW_MARGIN" <<'PY' || rc=$?
import sys
from pathlib import Path
import numpy as np
from PIL import Image

out, margin, preview_margin = Path(sys.argv[1]), float(sys.argv[2]), float(sys.argv[3])
INSET = 4
# GuiInventory player-model viewport at scale 2: 52x72 gui -> 104x144 fb.
PREVIEW_GUI = (24, 7, 52, 72)

def ysize(name):
    # GuiChest centers with ySize=168; drawn generic_54 composite is 167 tall.
    return 168 if name == "chest" else 166

def tex_h(name):
    return 167 if name == "chest" else 166

def panel_origin(w, h, name):
    s = max(1, h // 240)
    gw, gh = -(-w // s), -(-h // s)
    ys = ysize(name)
    x0 = (gw - 176) // 2 * s
    y0 = (gh - ys) // 2 * s
    return x0, y0, s

def panel_crop(img, name):
    a = np.asarray(img.convert("RGB")).astype(np.int16)
    h, w = a.shape[:2]
    x0, y0, s = panel_origin(w, h, name)
    th = tex_h(name)
    pw, pph = 176 * s, th * s
    i = INSET * s
    return a[y0 + i:y0 + pph - i, x0 + i:x0 + pw - i], (x0, y0, s)

def preview_crop(img, name="inventory"):
    a = np.asarray(img.convert("RGB")).astype(np.int16)
    h, w = a.shape[:2]
    x0, y0, s = panel_origin(w, h, name)
    gx, gy, gw, gh = PREVIEW_GUI
    return a[y0 + gy * s:y0 + (gy + gh) * s, x0 + gx * s:x0 + (gx + gw) * s]

def mean_abs(a, b, mask=None):
    d = np.abs(a.astype(np.int16) - b.astype(np.int16))
    return float(d[mask].mean() if mask is not None else d.mean())

fail = 0
print(f"{'screen':<14} {'noise(J-vs-J)':>13} {'magma-vs-J':>13} {'gate':>8}  verdict")
for name in ("table", "furnace", "inventory", "chest"):
    ja_path = out / f"mc_gui_{name}_a.png"
    jb_path = out / f"mc_gui_{name}_b.png"
    mag_path = out / f"magma_gui_{name}.ppm"
    if not ja_path.is_file() or not jb_path.is_file():
        print(f"{name:<14} {'--':>13} {'--':>13} {'--':>8}  FAIL (missing oracle golden; run capture_gui.sh)")
        print(f"  need: {ja_path.name} and {jb_path.name}")
        fail = 1
        continue
    if not mag_path.is_file():
        print(f"{name:<14} {'--':>13} {'--':>13} {'--':>8}  FAIL (missing magma render)")
        fail = 1
        continue
    ja, _ = panel_crop(Image.open(ja_path), name)
    jb, _ = panel_crop(Image.open(jb_path), name)
    c, _ = panel_crop(Image.open(mag_path), name)
    assert ja.shape == c.shape == jb.shape, (name, ja.shape, c.shape, jb.shape)
    noise = mean_abs(ja, jb)
    diff = mean_abs(ja, c)
    gate = noise + margin
    ok = diff <= gate
    # Inventory: whole-panel mean is informational only (dilution risk). Hard
    # gates are preview ROI + non-preview panel below.
    if name == "inventory":
        verdict = "INFO" if ok else "INFO-HIGH"
        print(f"{name:<14} {noise:>13.3f} {diff:>13.3f} {gate:>8.3f}  {verdict} (panel mean; not sole pass)")
    else:
        verdict = "PASS" if ok else "FAIL"
        fail |= not ok
        print(f"{name:<14} {noise:>13.3f} {diff:>13.3f} {gate:>8.3f}  {verdict}")
        if not ok:
            raw = np.abs(ja.astype(int) - c.astype(int)).clip(0, 255).astype(np.uint8)
            path = out / f"diff_gui_{name}.png"
            Image.fromarray(raw).save(path)
            print(f"  diff: {path}")

# --- inventory preview ROI (pose1: parked mouse 5,5) + non-preview panel ---
print("-- inventory preview ROI (104x144 @ scale2) + non-preview panel --")
ja_path = out / "mc_gui_inventory_a.png"
jb_path = out / "mc_gui_inventory_b.png"
mag_path = out / "magma_gui_inventory.ppm"
if not (ja_path.is_file() and jb_path.is_file() and mag_path.is_file()):
    print("inventory preview: FAIL (missing inventory golden or magma render)")
    fail = 1
else:
    prev_a = preview_crop(Image.open(ja_path))
    prev_b = preview_crop(Image.open(jb_path))
    prev_m = preview_crop(Image.open(mag_path))
    assert prev_a.shape == (144, 104, 3), prev_a.shape
    pnoise = mean_abs(prev_a, prev_b)
    pdiff = mean_abs(prev_a, prev_m)
    pgate = pnoise + preview_margin
    pok = pdiff <= pgate
    fail |= not pok
    print(f"{'preview pose1':<14} {pnoise:>13.3f} {pdiff:>13.3f} {pgate:>8.3f}  {'PASS' if pok else 'FAIL'}")
    print(f"  preview ROI exact: shape={prev_a.shape[1]}x{prev_a.shape[0]} "
          f"noise={pnoise:.6f} magma_vs_J={pdiff:.6f} gate={pgate:.6f} "
          f"margin={preview_margin}")
    if not pok:
        raw = np.abs(prev_a.astype(int) - prev_m.astype(int)).clip(0, 255).astype(np.uint8)
        path = out / "diff_gui_inventory_preview.png"
        Image.fromarray(raw).save(path)
        print(f"  diff: {path}")

    # Non-preview: inset panel minus the preview viewport.
    ja, (x0, y0, s) = panel_crop(Image.open(ja_path), "inventory")
    jb, _ = panel_crop(Image.open(jb_path), "inventory")
    cm, _ = panel_crop(Image.open(mag_path), "inventory")
    mask = np.ones(ja.shape[:2], dtype=bool)
    i = INSET * s
    gx, gy, gw, gh = PREVIEW_GUI
    # preview relative to inset crop origin (x0+i, y0+i)
    prx = gx * s - i
    pry = gy * s - i
    mask[pry:pry + gh * s, prx:prx + gw * s] = False
    nnoise = mean_abs(ja, jb, mask)
    ndiff = mean_abs(ja, cm, mask)
    ngate = nnoise + margin
    nok = ndiff <= ngate
    fail |= not nok
    print(f"{'non-preview':<14} {nnoise:>13.3f} {ndiff:>13.3f} {ngate:>8.3f}  {'PASS' if nok else 'FAIL'}")

# --- second fixed mouse pose (action_00 cursor on slot A) ---
print("-- inventory preview pose2 (mouse fb 282,258 / inv slot A) --")
pose2_j = out / "mc_gui_action_00_initial.png"
pose2_m = out / "magma_gui_inventory_pose2.ppm"
if not pose2_j.is_file():
    print("preview pose2: FAIL (missing mc_gui_action_00_initial.png; "
          "run capture_gui_actions.sh — fail-closed)")
    fail = 1
elif not pose2_m.is_file():
    print("preview pose2: FAIL (missing magma_gui_inventory_pose2.ppm)")
    fail = 1
else:
    # No _b for action frames: use inventory A/B preview noise as the floor.
    prev_a = preview_crop(Image.open(out / "mc_gui_inventory_a.png"))
    prev_b = preview_crop(Image.open(out / "mc_gui_inventory_b.png"))
    pnoise = mean_abs(prev_a, prev_b)
    p2j = preview_crop(Image.open(pose2_j))
    p2m = preview_crop(Image.open(pose2_m))
    p2diff = mean_abs(p2j, p2m)
    p2gate = pnoise + preview_margin
    p2ok = p2diff <= p2gate
    fail |= not p2ok
    print(f"{'preview pose2':<14} {pnoise:>13.3f} {p2diff:>13.3f} {p2gate:>8.3f}  {'PASS' if p2ok else 'FAIL'}")
    print(f"  preview pose2 exact: shape={p2j.shape[1]}x{p2j.shape[0]} "
          f"noise_ref={pnoise:.6f} magma_vs_J={p2diff:.6f} gate={p2gate:.6f}")
    if not p2ok:
        raw = np.abs(p2j.astype(int) - p2m.astype(int)).clip(0, 255).astype(np.uint8)
        path = out / "diff_gui_inventory_preview_pose2.png"
        Image.fromarray(raw).save(path)
        print(f"  diff: {path}")

sys.exit(1 if fail else 0)
PY
if [ "$rc" -eq 0 ]; then echo "gui verify: PASS"; else echo "gui verify: FAIL"; fi
exit "$rc"
