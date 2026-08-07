#!/usr/bin/env bash
# Render magma's deterministic inventory action sequence and pixel-diff each
# visible state against capture_gui_actions.sh's real-Minecraft PNG golden.
#
# Gate contract (adversarial-hardened; no mean+margin loophole):
#   - Owned pixels: full inventory panel (inset 4 gui px), every slot cell
#     (armor / craft 2x2 / result / offhand / main 27 / hotbar 9), hover
#     chrome, tooltips, and carried stack. Preview viewport is the only
#     panel hole (gated separately in run_gui_verify.sh).
#   - Compare at the Java A/B noise floor from mc_gui_inventory_{a,b}.png.
#     When A/B noise is zero, one wrong owned pixel fails (max channel +
#     hard-pixel checks; no noise+1 mean budget).
#   - OS cursor is a documented non-claim: neither the Java FBO golden nor
#     gm_screen_draw includes the OS pointer. No 12x12 hole is punched over
#     game pixels. If a candidate ever drew a synthetic cursor, strip it
#     before compare or accept the residual as FAIL — never hide game pixels.
#   - Mutation self-tests are non-vacuous: perfect base (PASS magma or Java
#     oracle copy), control must PASS, corruption must FAIL, paint counts
#     must be meaningful (one pixel / hundreds outside old ROIs / blank
#     armor+offhand / missing held stack / cursor-center).
#   - 08_close is state-only (focusdiag screen=None + capture present). After
#     close there is no inventory panel to pixel-claim.
set -euo pipefail
cd "$(dirname "$0")/../../magma"          # -> magma

BLAZE="$(cd ../blaze/core && pwd)"
OUT=../verify/mc_capture
FLAGS=(-O2 -ffp-contract=off -Wall -Wextra -I. -Icore -I"$BLAZE")
# Near-zero A/B noise ceiling (mean abs). Pin/capture must hit this or FAIL.
NOISE_MAX="${NOISE_MAX:-1e-6}"

echo "== build gui_actions_candidate =="
make -s game/screen.o game/player_preview.o game/hud.o game/item_render.o game/container_live.o game/runtime.o game/nbt_blob.o \
    game/fluid_live.o game/config.o game/player_ctl.o game/sel_box.o game/world_live.o \
    game/live_sim.o game/randtick.o game/mob_live.o game/dragon_live.o game/structures_live.o game/end_city_live.o game/end_population_live.o \
    game/portal_live.o game/furnace_live.o game/chest_live.o game/brewing_live.o game/enchanting_live.o game/caps.o core/config.o world/light.o world/mesh_mc.o \
    world/populate_mc.o world/blocks.o world/mesh.o world/world.o \
    renderkernels/rk_31_facebakery_make_quad.o assets/blockmodels.o core/math.o core/shade.o cpu/raster_cpu.o
gcc "${FLAGS[@]}" "$OUT/gui_actions_candidate.c" \
    game/screen.o game/player_preview.o game/hud.o game/item_render.o game/runtime.o game/nbt_blob.o game/fluid_live.o \
    game/config.o game/player_ctl.o game/sel_box.o game/world_live.o game/live_sim.o \
    game/randtick.o game/mob_live.o game/dragon_live.o game/structures_live.o game/end_city_live.o game/end_population_live.o game/portal_live.o \
    game/furnace_live.o game/chest_live.o game/brewing_live.o game/enchanting_live.o game/container_live.o game/caps.o core/config.o world/light.o \
    world/mesh_mc.o world/populate_mc.o world/blocks.o world/mesh.o world/world.o \
    renderkernels/rk_31_facebakery_make_quad.o assets/blockmodels.o \
    core/math.o core/shade.o cpu/raster_cpu.o -lm -o "$OUT/gui_actions_candidate"

echo "== render magma action states =="
"$OUT/gui_actions_candidate" "$OUT"

echo "== action pixel gate (owned panel/slots/cursor @ A/B noise; hard max) =="
uv run --no-project --with pillow --with numpy python - "$OUT" "$NOISE_MAX" <<'PY'
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image

out = Path(sys.argv[1])
noise_max_allowed = float(sys.argv[2]) if len(sys.argv) > 2 else 1e-6

STEPS = [
    "00_initial",
    "01_pickup_a",
    "02_place_b",
    "03_split_b",
    "04_deposit_one_c",
    "05_shift_b_to_hotbar",
    "06_swap_hotbar_0_1",
    "07_drop_one_hotbar0",
]
HELD_CURSOR = {"01_pickup_a", "03_split_b"}
HOVERED = {
    "00_initial": (282, 258),
    "01_pickup_a": (282, 258),
    "02_place_b": (318, 258),
    "03_split_b": (318, 258),
    "04_deposit_one_c": (354, 258),
    "05_shift_b_to_hotbar": (318, 258),
    "06_swap_hotbar_0_1": (282, 374),
    "07_drop_one_hotbar0": (282, 374),
}
# Legacy five ROIs (adversarial baseline): A/B/C + hotbar 0/1 only.
LEGACY_FIVE_CENTERS = [(282, 258), (318, 258), (354, 258), (282, 374), (318, 374)]
HARD_THR = 10.0
CELL = 16
PITCH = 18
PANEL_W, PANEL_H = 176, 166
PREVIEW_GUI = (24, 7, 52, 72)  # non-claim; run_gui_verify.sh owns preview
INSET = 4


def rgb(path):
    return np.asarray(Image.open(path).convert("RGB"), dtype=np.int16)


def panel_geom(shape):
    h, w = shape[:2]
    s = max(1, h // 240)
    gw, gh = -(-w // s), -(-h // s)
    x0 = (gw - PANEL_W) // 2 * s
    y0 = (gh - PANEL_H) // 2 * s
    return x0, y0, s


def slot_rects(shape):
    """Every player-inventory slot cell in framebuffer space."""
    x0, y0, s = panel_geom(shape)
    rects = []
    for k in range(4):
        rects.append(("armor", x0 + 8 * s, y0 + (8 + k * PITCH) * s, CELL * s, CELL * s))
    for i, (cx, cy) in enumerate(((0, 0), (1, 0), (0, 1), (1, 1))):
        rects.append(
            (
                "craft",
                x0 + (98 + cx * PITCH) * s,
                y0 + (18 + cy * PITCH) * s,
                CELL * s,
                CELL * s,
            )
        )
    rects.append(("result", x0 + 154 * s, y0 + 28 * s, CELL * s, CELL * s))
    rects.append(("offhand", x0 + 77 * s, y0 + 62 * s, CELL * s, CELL * s))
    for i in range(27):
        rects.append(
            (
                "main",
                x0 + (8 + (i % 9) * PITCH) * s,
                y0 + (84 + (i // 9) * PITCH) * s,
                CELL * s,
                CELL * s,
            )
        )
    for i in range(9):
        rects.append(
            (
                "hotbar",
                x0 + (8 + i * PITCH) * s,
                y0 + 142 * s,
                CELL * s,
                CELL * s,
            )
        )
    return rects


def held_stack_rect(shape, mx, my):
    """gm_screen_draw draws the carried stack at (mx-8s, my-8s), CELL x CELL."""
    h, w = shape[:2]
    s = max(1, h // 240)
    x = mx - 8 * s
    y = my - 8 * s
    return max(0, x), max(0, y), min(w, x + CELL * s) - max(0, x), min(h, y + CELL * s) - max(0, y)


def fill_rect(mask, x, y, ww, hh, value=True):
    h, w = mask.shape
    x0, y0 = max(0, x), max(0, y)
    x1, y1 = min(w, x + ww), min(h, y + hh)
    if x0 < x1 and y0 < y1:
        mask[y0:y1, x0:x1] = value


def owned_mask(shape, step=None):
    """Owned game pixels for this gate.

    Full panel chrome + every slot + hover/tooltip. Preview viewport is the
    only panel exclusion. Held-stack footprint is unioned when the step carries
    a cursor stack (already inside the panel for this sequence).

    OS cursor non-claim: empty mask hole. Neither FBO golden nor gm_screen_draw
    contains the OS pointer, so no fixed mask may hide game pixels under a
    synthetic cursor. (If a candidate drew one, strip it before compare.)
    """
    h, w = shape[:2]
    x0, y0, s = panel_geom(shape)
    mask = np.zeros((h, w), dtype=bool)
    i = INSET * s
    mask[y0 + i : y0 + PANEL_H * s - i, x0 + i : x0 + PANEL_W * s - i] = True
    pgx, pgy, pgw, pgh = PREVIEW_GUI
    mask[y0 + pgy * s : y0 + (pgy + pgh) * s, x0 + pgx * s : x0 + (pgx + pgw) * s] = False
    # Explicit slot union (armor/craft/result/offhand/main/hotbar) — must stay owned.
    for _name, x, y, ww, hh in slot_rects(shape):
        fill_rect(mask, x, y, ww, hh, True)
        # Re-apply preview cut only where a slot would illegally expand into it
        # (none of the vanilla slots do; assert below).
    if step in HELD_CURSOR:
        mx, my = HOVERED[step]
        hx, hy, hw, hh = held_stack_rect(shape, mx, my)
        fill_rect(mask, hx, hy, hw, hh, True)
        # Held stack never claims preview: if it overlapped, cut preview back out.
        mask[y0 + pgy * s : y0 + (pgy + pgh) * s, x0 + pgx * s : x0 + (pgx + pgw) * s] = False
    return mask


def legacy_five_roi_mask(shape):
    """Old five-ROI coverage (for mutation: corruption outside it must fail)."""
    h, w = shape[:2]
    m = np.zeros((h, w), dtype=bool)
    for x, y in LEGACY_FIVE_CENTERS:
        fill_rect(m, x - 22, y - 22, 44, 44, True)
    return m


def residual_stats(a, b, mask):
    d = np.abs(a.astype(np.int16) - b.astype(np.int16))
    per = d.max(axis=2)
    if not mask.any():
        return {
            "mean": 0.0,
            "max": 0.0,
            "hard_px": 0,
            "nz": 0,
            "owned_px": 0,
            "d": d,
            "per": per,
        }
    return {
        "mean": float(d[mask].mean()),
        "max": float(per[mask].max()),
        "hard_px": int((per[mask] >= HARD_THR).sum()),
        "nz": int((per[mask] > 0).sum()),
        "owned_px": int(mask.sum()),
        "d": d,
        "per": per,
    }


def gate_ok(st, noise_mean, noise_max_ch):
    """Hard contract: residual at A/B noise floor; one wrong owned pixel fails
    when noise is zero (max channel check)."""
    if noise_mean > noise_max_allowed or noise_max_ch > HARD_THR:
        return False
    return (
        st["mean"] <= noise_mean + 1e-6
        and st["max"] <= noise_max_ch + 1e-9
        and st["hard_px"] == 0
    )


def region_residuals(per, shape):
    rows = []
    counts = {}
    for name, x, y, ww, hh in slot_rects(shape):
        idx = counts.get(name, 0)
        counts[name] = idx + 1
        sub = per[y : y + hh, x : x + ww]
        label = f"{name}{idx}@{x},{y}"
        rows.append(
            (
                label,
                int((sub > 0).sum()),
                float(sub.max()) if sub.size else 0.0,
                int((sub >= HARD_THR).sum()),
            )
        )
    return rows


def assert_slot_coverage(shape):
    base = owned_mask(shape, step=None)
    missing = []
    for name, x, y, ww, hh in slot_rects(shape):
        cell = np.zeros(shape[:2], dtype=bool)
        fill_rect(cell, x, y, ww, hh, True)
        if not np.all(base[cell]):
            missing.append(name)
    if missing:
        raise SystemExit(f"owned mask misses slot region(s): {missing}")
    # Offhand must not sit inside the preview hole.
    off = next(r for r in slot_rects(shape) if r[0] == "offhand")
    _, ox, oy, ow, oh = off
    x0, y0, s = panel_geom(shape)
    pgx, pgy, pgw, pgh = PREVIEW_GUI
    prev = np.zeros(shape[:2], dtype=bool)
    fill_rect(prev, x0 + pgx * s, y0 + pgy * s, pgw * s, pgh * s, True)
    cell = np.zeros(shape[:2], dtype=bool)
    fill_rect(cell, ox, oy, ow, oh, True)
    if np.any(cell & prev):
        raise SystemExit("offhand overlaps preview non-claim; gate would hide shield slot")


def paint_rect(img, x, y, ww, hh, color):
    out_img = img.copy()
    h, w = out_img.shape[:2]
    x0, y0 = max(0, x), max(0, y)
    x1, y1 = min(w, x + ww), min(h, y + hh)
    out_img[y0:y1, x0:x1] = color
    return out_img


def paint_rect_count(img, x, y, ww, hh, color, mask=None):
    """Paint a rect; return (img, n_owned_pixels that actually changed)."""
    out_img = img.copy()
    h, w = out_img.shape[:2]
    x0, y0 = max(0, x), max(0, y)
    x1, y1 = min(w, x + ww), min(h, y + hh)
    if x0 >= x1 or y0 >= y1:
        return out_img, 0
    region = np.zeros((h, w), dtype=bool)
    region[y0:y1, x0:x1] = True
    if mask is not None:
        region &= mask
    before = out_img[region]
    color_a = np.asarray(color, dtype=out_img.dtype)
    changed = int(np.any(before != color_a, axis=1).sum()) if before.size else 0
    out_img[region] = color
    return out_img, changed


def perfect_base(oracle_img, magma_img, step, noise_mean, noise_max_ch):
    """Non-vacuous control image that PASSes the hard gate.

    Prefer a genuine magma PASS step; otherwise use the Java oracle copy so
    residual is bit-exact under the owned mask (oracle vs oracle).
    """
    mask = owned_mask(oracle_img.shape, step)
    st = residual_stats(oracle_img, magma_img, mask)
    if gate_ok(st, noise_mean, noise_max_ch):
        return magma_img.copy(), "magma_pass"
    return oracle_img.copy(), "oracle_copy"


def run_mutations(oracle, magma, noise_mean, noise_max_ch):
    """Prove the gate catches corruptions the old mean+5-ROI hole allowed.

    Non-vacuous protocol per case:
      1) build a perfect passing base (genuine PASS magma or Java oracle copy)
      2) assert unmutated control PASSes
      3) apply the intended corruption
      4) assert mutated FAILs
      5) require meaningful painted/changed owned counts for bulk cases
    """
    shape = oracle["00_initial"].shape
    om0 = owned_mask(shape, "00_initial")
    legacy = legacy_five_roi_mask(shape)
    failed = False
    cases = []

    def eval_case(name, step, base, mask, mut, n_paint, min_paint, note):
        """Control must PASS; mutated must FAIL; paint count must be meaningful."""
        st_ctrl = residual_stats(oracle[step], base, mask)
        ctrl_ok = gate_ok(st_ctrl, noise_mean, noise_max_ch)
        st_mut = residual_stats(oracle[step], mut, mask)
        mut_ok = gate_ok(st_mut, noise_mean, noise_max_ch)
        paint_ok = n_paint >= min_paint
        # Residual must actually move: nz after mutation covers the paint.
        residual_ok = st_mut["nz"] >= min(min_paint, max(1, n_paint))
        caught = ctrl_ok and (not mut_ok) and paint_ok and residual_ok
        detail = (
            f"ctrl={'PASS' if ctrl_ok else 'FAIL'} "
            f"mut={'FAIL' if not mut_ok else 'PASS'} "
            f"n_paint={n_paint} (need>={min_paint}) "
            f"nz={st_mut['nz']}"
        )
        if note:
            detail = f"{note}; {detail}"
        cases.append((name, caught, st_ctrl, st_mut, detail))
        return caught

    # 1) Single owned pixel wrong (prefer outside old five ROIs).
    base0, src0 = perfect_base(
        oracle["00_initial"], magma["00_initial"], "00_initial", noise_mean, noise_max_ch
    )
    ys, xs = np.where(om0)
    outside = ~legacy[ys, xs]
    idx = int(np.flatnonzero(outside)[0]) if outside.any() else 0
    y, x = int(ys[idx]), int(xs[idx])
    mut = base0.copy()
    orig = mut[y, x].copy()
    # Force a channel-large difference so hard_px fires when noise is zero.
    mut[y, x] = (255 if orig[0] < 128 else 0, 0, 0)
    if np.array_equal(mut[y, x], orig):
        mut[y, x] = (0, 255, 0)
    n_paint = 0 if np.array_equal(mut[y, x], orig) else 1
    if not eval_case(
        "one_pixel",
        "00_initial",
        base0,
        om0,
        mut,
        n_paint,
        1,
        f"base={src0} @({x},{y})",
    ):
        failed = True

    # 2) Hundreds of wrong pixels outside the old five ROIs (armor, then craft).
    base0, src0 = perfect_base(
        oracle["00_initial"], magma["00_initial"], "00_initial", noise_mean, noise_max_ch
    )
    mut = base0.copy()
    paint_mask = np.zeros(shape[:2], dtype=bool)
    # Paint every armor cell outside the old five ROIs; expand to craft if
    # armor alone cannot reach the bulk floor (require >=200 changed owned px).
    for want in ("armor", "craft"):
        for _n, ax, ay, aw, ah in slot_rects(shape):
            if _n != want:
                continue
            region = np.zeros(shape[:2], dtype=bool)
            fill_rect(region, ax, ay, aw, ah, True)
            region &= om0 & ~legacy
            if not region.any():
                continue
            paint_mask |= region
            mut[region] = (255, 0, 0)
        if int(paint_mask.sum()) >= 200 and want == "armor":
            break  # armor alone is enough; keep craft for expansion only
    # Count pixels that actually differ from oracle under owned∩paint.
    changed = int(
        np.any(mut[paint_mask] != oracle["00_initial"][paint_mask], axis=1).sum()
    ) if paint_mask.any() else 0
    n_paint = changed
    if not eval_case(
        "hundreds_outside_five_rois",
        "00_initial",
        base0,
        om0,
        mut,
        n_paint,
        200,
        f"base={src0}",
    ):
        failed = True

    # 3) Blank armor + offhand empty icons (product owns those sprites).
    base0, src0 = perfect_base(
        oracle["00_initial"], magma["00_initial"], "00_initial", noise_mean, noise_max_ch
    )
    mut = base0.copy()
    n_paint = 0
    for name, ax, ay, aw, ah in slot_rects(shape):
        if name in ("armor", "offhand"):
            mut, n_ch = paint_rect_count(mut, ax, ay, aw, ah, (0, 0, 0), om0)
            n_paint += n_ch
    if not eval_case(
        "blank_armor_offhand",
        "00_initial",
        base0,
        om0,
        mut,
        n_paint,
        100,
        f"base={src0}",
    ):
        failed = True

    # 4) Missing held stack on pickup (genuine PASS step preferred).
    base1, src1 = perfect_base(
        oracle["01_pickup_a"], magma["01_pickup_a"], "01_pickup_a", noise_mean, noise_max_ch
    )
    om1 = owned_mask(shape, "01_pickup_a")
    hx, hy, hw, hh = held_stack_rect(shape, *HOVERED["01_pickup_a"])
    mut, n_paint = paint_rect_count(base1, hx, hy, hw, hh, (120, 120, 120), om1)
    if not eval_case(
        "missing_held_stack",
        "01_pickup_a",
        base1,
        om1,
        mut,
        n_paint,
        16,
        f"base={src1} held=({hx},{hy},{hw},{hh})",
    ):
        failed = True

    # 5) Cursor-center corruption (old 12x12 empty-cursor hole over game pixels).
    base0, src0 = perfect_base(
        oracle["00_initial"], magma["00_initial"], "00_initial", noise_mean, noise_max_ch
    )
    mx, my = HOVERED["00_initial"]
    mut, n_paint = paint_rect_count(base0, mx - 6, my - 6, 12, 12, (255, 0, 255), om0)
    if not eval_case(
        "cursor_center_corruption",
        "00_initial",
        base0,
        om0,
        mut,
        n_paint,
        12,
        f"base={src0} 12x12@({mx},{my})",
    ):
        failed = True

    print(
        "-- mutation self-tests (non-vacuous: control PASS, then corruption FAIL) --"
    )
    print(
        f"{'case':<28} {'ctrl_nz':>7} {'mut_mean':>10} {'mut_max':>7} "
        f"{'hard':>6} {'nz':>7}  verdict"
    )
    for name, caught, st_ctrl, st_mut, note in cases:
        verdict = "CAUGHT" if caught else "MISSED"
        if not caught:
            failed = True
        print(
            f"{name:<28} {st_ctrl['nz']:7d} {st_mut['mean']:10.6f} "
            f"{st_mut['max']:7.1f} {st_mut['hard_px']:6d} {st_mut['nz']:7d}  "
            f"{verdict}  {note}"
        )
    if failed:
        print(
            "MUTATION SELF-TEST FAIL: vacuous control, weak paint, or gate miss"
        )
    else:
        print(
            "mutation self-tests: PASS "
            "(control PASS + five corruptions rejected with paint counts)"
        )
    return not failed


# --- load goldens / candidates ---
ja = rgb(out / "mc_gui_inventory_a.png")
jb = rgb(out / "mc_gui_inventory_b.png")
assert_slot_coverage(ja.shape)

noise_mask = owned_mask(ja.shape, step=None)
noise_st = residual_stats(ja, jb, noise_mask)
noise_mean = noise_st["mean"]
noise_max_ch = noise_st["max"]
print(
    f"A/B owned noise: mean={noise_mean:.6g} max={noise_max_ch:.6g} "
    f"hard_px={noise_st['hard_px']} owned_px={noise_st['owned_px']} "
    f"(ceiling mean<={noise_max_allowed:g})"
)
if noise_mean > noise_max_allowed:
    print(
        f"FAIL: A/B noise mean {noise_mean:.6g} > {noise_max_allowed:g} "
        "(pin/capture broken; not absorbed into a pass budget)"
    )
    raise SystemExit(1)

oracle = {}
magma = {}
for step in STEPS:
    op = out / f"mc_gui_action_{step}.png"
    mp = out / f"magma_gui_action_{step}.ppm"
    oracle[step] = rgb(op)
    magma[step] = rgb(mp)
    if oracle[step].shape != magma[step].shape or oracle[step].shape != ja.shape:
        raise SystemExit(
            f"{step}: shape mismatch {oracle[step].shape} {magma[step].shape} {ja.shape}"
        )

mut_ok = run_mutations(oracle, magma, noise_mean, noise_max_ch)

print(
    f"-- per-step owned gate (rule: mean<=noise+1e-6, max<=noise_max, hard_px==0; "
    f"HARD_THR={HARD_THR}) --"
)
print(
    f"{'step':<27} {'mean':>10} {'max':>6} {'hard':>6} {'nz':>7} "
    f"{'owned':>7}  verdict"
)
failed = not mut_ok
residuals = []
for step in STEPS:
    mask = owned_mask(oracle[step].shape, step)
    st = residual_stats(oracle[step], magma[step], mask)
    ok = gate_ok(st, noise_mean, noise_max_ch)
    verdict = "PASS" if ok else "FAIL/OPEN"
    print(
        f"{step:<27} {st['mean']:10.6f} {st['max']:6.1f} {st['hard_px']:6d} "
        f"{st['nz']:7d} {st['owned_px']:7d}  {verdict}"
    )
    residuals.append((step, st, ok))
    if not ok:
        failed = True
        d = st["d"]
        per = st["per"]
        print(
            f"  residual: mean={st['mean']:.6f} max={st['max']:.3f} "
            f"hard_px={st['hard_px']} nz={st['nz']} "
            f"(noise mean={noise_mean:.6g} max={noise_max_ch:.6g}; bit-exact required)"
        )
        for name, nz, mx, hard in region_residuals(per, oracle[step].shape):
            if nz:
                print(f"  region {name}: nz={nz} max={mx:.1f} hard={hard}")
        diff = np.clip(d, 0, 255).astype(np.uint8)
        diff_path = out / f"diff_gui_action_{step}.png"
        Image.fromarray(diff).save(diff_path)
        print(f"  diff: {diff_path}")

meta = json.load(open(out / "gui_actions_scene.json"))
close_path = out / "mc_gui_action_08_close.png"
close_ok = close_path.exists() and Image.open(close_path).size == (854, 480)
close_ok &= meta.get("close_focusdiag", {}).get("screen") is None
# Honest state-only wording: no pixel residual is claimed after close.
print(
    f"{'08_close':<27} {'--':>10} {'--':>6} {'--':>6} {'--':>7} {'--':>7}  "
    f"{'PASS' if close_ok else 'FAIL'} "
    f"(state-only: focusdiag screen=None + capture present; no pixel claim)"
)
failed |= not close_ok

print("-- residual summary (owned panel/slots/cursor) --")
for step, st, ok in residuals:
    status = "PASS" if ok else "OPEN"
    print(
        f"  {step}: {status} mean={st['mean']:.6f} max={st['max']:.3f} "
        f"hard_px={st['hard_px']} nz={st['nz']}"
    )

raise SystemExit(1 if failed else 0)
PY
