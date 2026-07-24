#!/usr/bin/env bash
# run_gui_verify.sh - the container-screen pixel gate: render each GUI screen
# (crafting table / furnace / player inventory / single chest) through the real
# gm_screen_draw path (gui_candidate.c) and pixel-diff against REAL Minecraft
# goldens from capture_gui.sh (mc_gui_*_a.png).
#
# Gate design (PRODUCT.md "Visual acceptance"):
#   - Panel region is inset by 4px per side (rounded corners over the 3D scene).
#   - Tolerance is CALIBRATED: Java-vs-Java repeat (_a vs _b) + measured allowance.
#   - Inventory: dedicated 104x144 (scale-2) player-preview ROI is a HARD gate.
#     Preview A/B goldens must be captured under qrl pin_preview_anim (ageInTicks=0).
#     Raster-vs-GL allowance is measured from an edge-band control on the same
#     frames (not an arbitrary +1.0 margin). Residual clusters are reported;
#     "pixel-perfect" is only claimed when residual is at the J-vs-J noise floor.
#   - Pose2 (mouse on inv slot A) has its own _a/_b noise (mc_gui_inventory_pose2_*
#     or mc_gui_action_00_initial{,_b}).
#   - Slot/cursor ROIs live in run_gui_actions_verify.sh and do NOT claim preview.
#   - Chest fails clearly if mc_gui_chest_{a,b}.png are absent (no fabricate).
#   - Not implemented: dispenser/dropper/hopper/enchant/brew/anvil/villager/
#     creative/beacon/horse/shulker.
set -euo pipefail
cd "$(dirname "$0")/../../.."          # -> c/magma

MCSIM="$(cd ../mc-sim/core && pwd)"
OUT=raster/verify/mc_capture
FLAGS=(-O2 -ffp-contract=off -Wall -Wextra -I. -Icore -I"$MCSIM")
# 2D panel chrome (table/furnace) still uses a small absolute margin on top of
# J-vs-J noise: those screens are bit-exact today; margin only absorbs capture
# quantization. Preview does NOT use this — see PREVIEW path below.
MARGIN="${MARGIN:-1.0}"
# Optional override for the measured raster-vs-GL mean allowance (mean-abs).
# Default: load gui_preview_calibration.json (pose1 control under pin).
PREVIEW_RASTER_ALLOWANCE="${PREVIEW_RASTER_ALLOWANCE:-}"
CALIB="$OUT/gui_preview_calibration.json"

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

# Second fixed mouse pose: empty inventory (same as pose1 golden) with mouse on
# inv slot A (fb 282,258). Must NOT reuse gui_actions_candidate — that loadout
# has items and is a different scene than capture_gui pose2.
echo "== render inventory preview pose2 (empty inv, mouse on slot A) =="
"$OUT/gui_candidate" --container 0 --w 854 --h 480 --mx 282 --my 258 \
    --ppm "$OUT/magma_gui_inventory_pose2.ppm"

echo "implemented magma screens: inventory, crafting table, furnace, chest"
echo "not implemented: dispenser/dropper, hopper, enchanting, brewing, anvil, villager, creative, beacon, horse, shulker"

echo "== panel + preview ROI pixel diff =="
rc=0
uv run --no-project --with pillow --with numpy python - "$OUT" "$MARGIN" "${PREVIEW_RASTER_ALLOWANCE}" "$CALIB" <<'PY' || rc=$?
import json
import sys
from pathlib import Path
import numpy as np
from PIL import Image

out = Path(sys.argv[1])
margin = float(sys.argv[2])
allow_override = sys.argv[3].strip() if len(sys.argv) > 3 else ""
calib_path = Path(sys.argv[4]) if len(sys.argv) > 4 else out / "gui_preview_calibration.json"
INSET = 4
# GuiInventory player-model viewport at scale 2: 52x72 gui -> 104x144 fb.
PREVIEW_GUI = (24, 7, 52, 72)

# Load measured raster-vs-GL floor from explicit pose1 control calibration.
# NOT an arbitrary +1.0. See gui_preview_calibration.json provenance.
if calib_path.is_file():
    calib = json.loads(calib_path.read_text())
    MEAN_ALLOWANCE = float(calib["gate"]["mean_allowance"])
    HARD_PX_CAP = int(calib["gate"]["hard_px_cap"])
    HARD_THR = float(calib["gate"].get("hard_thr", 10.0))
    CALIB_SRC = str(calib_path.name)
else:
    MEAN_ALLOWANCE, HARD_PX_CAP, HARD_THR = 0.20, 16, 10.0
    CALIB_SRC = "builtin-fallback (missing calibration file)"

if allow_override:
    MEAN_ALLOWANCE = float(allow_override)
    CALIB_SRC = f"override MEAN={MEAN_ALLOWANCE}"

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

def per_px(a, b):
    return np.abs(a.astype(np.int16) - b.astype(np.int16)).mean(axis=2)

def residual_clusters(d, thr=10.0, min_px=8):
    """4-connected clusters of pixels with mean-abs > thr. Returns list of
    dicts {n, mean, max, bbox=(x0,y0,x1,y1)} sorted by n desc."""
    H, W = d.shape
    vis = np.zeros_like(d, dtype=bool)
    hot = d > thr
    clusters = []
    ys, xs = np.where(hot)
    for y0, x0 in zip(ys, xs):
        if vis[y0, x0]:
            continue
        stack = [(y0, x0)]
        vis[y0, x0] = True
        cells = []
        while stack:
            y, x = stack.pop()
            cells.append((y, x))
            for dy, dx in ((0, 1), (0, -1), (1, 0), (-1, 0)):
                ny, nx = y + dy, x + dx
                if 0 <= ny < H and 0 <= nx < W and hot[ny, nx] and not vis[ny, nx]:
                    vis[ny, nx] = True
                    stack.append((ny, nx))
        if len(cells) < min_px:
            continue
        vals = np.array([d[y, x] for y, x in cells])
        ys_c = [y for y, x in cells]
        xs_c = [x for y, x in cells]
        clusters.append({
            "n": len(cells),
            "mean": float(vals.mean()),
            "max": float(vals.max()),
            "bbox": (min(xs_c), min(ys_c), max(xs_c), max(ys_c)),
        })
    clusters.sort(key=lambda c: -c["n"])
    return clusters

def gate_preview(label, ja, jb, jm):
    """Gate against pose-specific J-vs-J noise + measured raster-vs-GL floor.

    Dual criteria (both required):
      1. mean_abs(J, magma) <= noise + MEAN_ALLOWANCE
      2. hard_px (per-pixel mean-abs >= HARD_THR) <= HARD_PX_CAP

    MEAN_ALLOWANCE / HARD_PX_CAP come from gui_preview_calibration.json
    (pose1 control under pin_preview_anim), not from convention.
    """
    assert ja.shape == jm.shape == jb.shape, (label, ja.shape, jm.shape, jb.shape)
    noise = mean_abs(ja, jb)
    diff = mean_abs(ja, jm)
    d = per_px(ja, jm)
    hard_px = int((d >= HARD_THR).sum())
    gate = noise + MEAN_ALLOWANCE
    mean_ok = diff <= gate
    hard_ok = hard_px <= HARD_PX_CAP
    ok = mean_ok and hard_ok
    pixel_perfect = diff <= noise + 1e-6 and hard_px == 0
    clusters = residual_clusters(d, thr=HARD_THR, min_px=8)
    if pixel_perfect:
        verdict = "PASS"
    elif ok:
        verdict = "PASS-FLOOR"
    else:
        verdict = "FAIL"
    print(f"{label:<14} {noise:>13.3f} {diff:>13.3f} {gate:>8.3f}  {verdict}")
    print(f"  preview ROI exact: shape={ja.shape[1]}x{ja.shape[0]} "
          f"noise={noise:.6f} magma_vs_J={diff:.6f} gate={gate:.6f}")
    print(f"  raster-vs-GL: mean_allowance={MEAN_ALLOWANCE:.6f} hard_px={hard_px} "
          f"hard_cap={HARD_PX_CAP} hard_thr={HARD_THR} mean_ok={mean_ok} "
          f"hard_ok={hard_ok} calib={CALIB_SRC}")
    print(f"  dist: p50={float(np.median(d)):.3f} p95={float(np.percentile(d,95)):.3f} "
          f"p99={float(np.percentile(d,99)):.3f} max={float(d.max()):.3f} "
          f"px>1={(d > 1).sum()} px>5={(d > 5).sum()}")
    if pixel_perfect:
        print("  claim: pixel-perfect (residual at J-vs-J noise floor)")
    else:
        print("  claim: NOT pixel-perfect; residual above capture noise "
              f"(extra={diff - noise:.6f})")
    if clusters:
        print(f"  residual clusters (thr={HARD_THR}, min_px=8): {len(clusters)}")
        for i, c in enumerate(clusters[:12]):
            x0, y0, x1, y1 = c["bbox"]
            print(f"    [{i}] n={c['n']} mean={c['mean']:.1f} max={c['max']:.1f} "
                  f"bbox=({x0},{y0})-({x1},{y1})")
        if len(clusters) > 12:
            print(f"    ... +{len(clusters) - 12} more")
    else:
        print(f"  residual clusters: none above thr={HARD_THR} / min_px=8")
    return ok, diff, noise, clusters

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
print("  (goldens must be pin_preview_anim ageInTicks=0; see capture_gui.sh)")
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
    pok, _, _, _ = gate_preview("preview pose1", prev_a, prev_b, prev_m)
    fail |= not pok
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
    prx = gx * s - i
    pry = gy * s - i
    mask[pry:pry + gh * s, prx:prx + gw * s] = False
    nnoise = mean_abs(ja, jb, mask)
    ndiff = mean_abs(ja, cm, mask)
    ngate = nnoise + margin
    nok = ndiff <= ngate
    fail |= not nok
    print(f"{'non-preview':<14} {nnoise:>13.3f} {ndiff:>13.3f} {ngate:>8.3f}  {'PASS' if nok else 'FAIL'}")

# --- second fixed mouse pose (slot A) with its OWN A/B noise ---
print("-- inventory preview pose2 (mouse fb 282,258 / inv slot A, empty inv) --")
pose2_ja = out / "mc_gui_inventory_pose2_a.png"
pose2_jb = out / "mc_gui_inventory_pose2_b.png"
pose2_m = out / "magma_gui_inventory_pose2.ppm"
if not pose2_ja.is_file() or not pose2_jb.is_file():
    print("preview pose2: FAIL (missing pose2 A/B goldens; run capture_gui.sh "
          "— fail-closed, no borrowed inventory noise)")
    print("  need: mc_gui_inventory_pose2_{a,b}.png")
    fail = 1
elif not pose2_m.is_file():
    print("preview pose2: FAIL (missing magma_gui_inventory_pose2.ppm)")
    fail = 1
else:
    p2a = preview_crop(Image.open(pose2_ja))
    p2b = preview_crop(Image.open(pose2_jb))
    p2m = preview_crop(Image.open(pose2_m))
    p2ok, _, _, _ = gate_preview("preview pose2", p2a, p2b, p2m)
    fail |= not p2ok
    if not p2ok:
        raw = np.abs(p2a.astype(int) - p2m.astype(int)).clip(0, 255).astype(np.uint8)
        path = out / "diff_gui_inventory_preview_pose2.png"
        Image.fromarray(raw).save(path)
        print(f"  diff: {path}")

sys.exit(1 if fail else 0)
PY
if [ "$rc" -eq 0 ]; then echo "gui verify: PASS"; else echo "gui verify: FAIL"; fi
exit "$rc"
