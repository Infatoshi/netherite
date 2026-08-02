#!/usr/bin/env bash
# run_hard_scene.sh — scene-level integration golden for the hard leaf canopy case.
#
# Not a kernel unit test. Renders the frozen seed-0 aerial forest pose through the
# full magma stack (worldgen → mesh → shade → software raster) and diffs against
# REAL MC (mc_frame.png). Runs a small ablation board so regressions name which
# composition knob moved the residual (smooth AO / fog / gamma).
#
# Hard scene (see hard_scene.json):
#   seed 0, eye (8.3, 95.0, 40.5), magma yaw 0 / pitch -35, 854x480 FOV 77
#
# Gates:
#   REGRESS  — crop must stay under measured floor + slack. Fail = break.
#   TARGET   — crop ≤15 is the parity bar. Reported; does not fail the script until
#              HARD_SCENE_TARGET=1 (then crop>15 fails).
#
# Usage (from magma):
#   bash ../verify/mc_capture/run_hard_scene.sh
#   make hard-scene-verify
#   HARD_SCENE_TARGET=1 make hard-scene-verify   # fail until crop≤15
set -euo pipefail
cd "$(dirname "$0")/../../magma"   # -> magma

BLAZE="$(cd ../blaze/core && pwd)"
DIFF="$(cd ../java/render-opt/wholeframe && pwd)/diff_frame.py"
OUT=../verify/mc_capture
GOLDEN="$OUT/mc_frame.png"
REPORT_DIR="${HARD_SCENE_OUT:-/tmp/hard_scene_seed0}"
FLAGS=(-O2 -ffp-contract=off -Wall -Icore -I. -I"$BLAZE")

# Pose: prefer instrumented camera.json (JVM dump) over hand numbers.
# Fallback matches last recapture: eye (8.3,95,40.5), FOV 77 = 70 * spectator fly 1.1.
SEED=0
EYE_X=8.3 EYE_Y=95.0 EYE_Z=40.5
YAW=0 PITCH=-35 FOV=77
W=854 H=480
if [ -s "$OUT/camera.json" ]; then
  # shellcheck disable=SC2046
  eval $(uv run --no-project python - "$OUT/camera.json" <<'PY'
import json,sys
c=json.load(open(sys.argv[1]))
print(f"EYE_X={c.get('eye_x',8.3)}")
print(f"EYE_Y={c.get('eye_y',95.0)}")
print(f"EYE_Z={c.get('eye_z',40.5)}")
# magma yaw/pitch degrees
print(f"YAW={c.get('magma_yaw_deg',0.0)}")
print(f"PITCH={c.get('magma_pitch_deg',-35.0)}")
print(f"FOV={c.get('fov_effective',77.0)}")
if c.get("display_w"): print(f"W={int(c['display_w'])}")
if c.get("display_h"): print(f"H={int(c['display_h'])}")
print("CAMERA_JSON=1")
PY
)
  echo "    camera.json: eye ($EYE_X,$EYE_Y,$EYE_Z) magma_yaw=$YAW pitch=$PITCH fov_eff=$FOV"
fi
TCROP="180:479,180:674"          # terrain crop (honest ground band)
CCROP="120:360,200:650"          # canopy band (leaf silhouette stress)

# Gates: measured FOV77+z40.5 crop ~13; keep regress headroom, target ≤15.
REGRESS_WHOLE="${REGRESS_WHOLE:-40.0}"
REGRESS_CROP="${REGRESS_CROP:-18.0}"
TARGET_CROP="${TARGET_CROP:-15.0}"
HARD_SCENE_TARGET="${HARD_SCENE_TARGET:-0}"

if [ ! -s "$GOLDEN" ]; then
  echo "ERROR: missing golden $GOLDEN (run capture.sh once)"
  exit 1
fi

mkdir -p "$REPORT_DIR"

echo "== hard-scene: seed0 leaf canopy (scene-level integration golden) =="
echo "    golden : $GOLDEN"
echo "    out    : $REPORT_DIR"
echo "    pose   : eye ($EYE_X,$EYE_Y,$EYE_Z) yaw=$YAW pitch=$PITCH fov=$FOV ${W}x${H}"
echo "    crops  : terrain=$TCROP  canopy=$CCROP"
echo

# Cumulative populate order (real lever): qrl_0 spawn (44,176) -> chunk (2,11).
# Prefer recorded genprobe prep list when present (exact vanilla order).
export MAGMA_SPAWN_CX="${MAGMA_SPAWN_CX:-2}"
export MAGMA_SPAWN_CZ="${MAGMA_SPAWN_CZ:-11}"
if [ -z "${MAGMA_PREP_LIST:-}" ]; then
  for cand in \
      "$REPORT_DIR/wv_prep_list.txt" \
      /tmp/frustum_wv2/wv_prep_list.txt \
      trace/out/wv_prep_list.txt; do
    if [ -s "$cand" ]; then
      export MAGMA_PREP_LIST="$cand"
      break
    fi
  done
fi
echo "    prepare: spawn=($MAGMA_SPAWN_CX,$MAGMA_SPAWN_CZ) prep_list=${MAGMA_PREP_LIST:-derived}"
echo

echo "== build game_candidate =="
for u in world/mesh_mc world/light world/populate_mc assets/blockmodels \
         renderkernels/rk_31_facebakery_make_quad game/sky game/caps \
         core/math core/shade cpu/raster_cpu transform; do
  gcc "${FLAGS[@]}" -c "$u.c" -o "$u.o"
done
gcc "${FLAGS[@]}" ../verify/mc_capture/game_candidate.c \
    world/mesh_mc.o world/light.o world/populate_mc.o assets/blockmodels.o \
    renderkernels/rk_31_facebakery_make_quad.o game/sky.o game/caps.o core/config.o core/math.o \
    core/shade.o cpu/raster_cpu.o transform.o \
    -o /tmp/hard_scene_candidate -lm

render_variant() {
  local name="$1"
  shift
  local ppm="$REPORT_DIR/cand_${name}.ppm"
  local png="$REPORT_DIR/cand_${name}.png"
  # env vars for this variant are already exported by caller, or passed as env PREFIX
  env "$@" /tmp/hard_scene_candidate \
      --seed "$SEED" --eye "$EYE_X" "$EYE_Y" "$EYE_Z" \
      --yaw "$YAW" --pitch "$PITCH" --fov "$FOV" --w "$W" --h "$H" \
      --ppm "$ppm" >"$REPORT_DIR/cand_${name}.log" 2>&1
  uv run --no-project --with pillow python -c \
    "from PIL import Image; Image.open('$ppm').convert('RGB').save('$png')"
  echo "$png"
}

echo "== render ablations =="
# baseline: measured best (smooth off; fog ON by default / Java setupFog)
BASE_PNG=$(render_variant default)
# smooth AO (MC fancy lighting path)
SMOOTH_PNG=$(MAGMA_SMOOTH=1 render_variant smooth MAGMA_SMOOTH=1)
# terrain fog
FOG_PNG=$(MAGMA_FOG=1 render_variant fog MAGMA_FOG=1)
# fog off escape (pre-default baseline)
NOFOG_PNG=$(MAGMA_FOG=0 render_variant nofog MAGMA_FOG=0)
# gamma 2.0 (MC-ish curve; often worsens crop when mis-applied)
GAMMA_PNG=$(MAGMA_GAMMA=2.0 render_variant gamma2 MAGMA_GAMMA=2.0)
# smooth + fog (interaction)
BOTH_PNG=$(MAGMA_SMOOTH=1 MAGMA_FOG=1 render_variant smooth_fog MAGMA_SMOOTH=1 MAGMA_FOG=1)

echo
echo "== scoreboard (vs REAL MC golden) =="
uv run --no-project --with numpy --with pillow python - \
  "$DIFF" "$GOLDEN" "$TCROP" "$CCROP" \
  "$REGRESS_WHOLE" "$REGRESS_CROP" "$TARGET_CROP" "$HARD_SCENE_TARGET" \
  "$REPORT_DIR" \
  default "$BASE_PNG" \
  smooth "$SMOOTH_PNG" \
  fog "$FOG_PNG" \
  nofog "$NOFOG_PNG" \
  gamma2 "$GAMMA_PNG" \
  smooth_fog "$BOTH_PNG" \
  <<'PY'
import json, os, subprocess, sys
from pathlib import Path

diff, golden, tcrop, ccrop = sys.argv[1:5]
reg_w = float(sys.argv[5])
reg_c = float(sys.argv[6])
tgt_c = float(sys.argv[7])
hard_tgt = int(sys.argv[8])
report_dir = sys.argv[9]
rest = sys.argv[10:]
variants = list(zip(rest[0::2], rest[1::2]))

def measure(cand, crop):
    out = subprocess.check_output(
        ["python", diff, golden, cand, "--crop", crop, "--json"],
        text=True,
    )
    j = json.loads(out)["comparisons"][0]
    if crop == "none":
        return j["whole"]
    return j["crop"]

rows = []
for name, path in variants:
    whole = measure(path, "none")
    terr = measure(path, tcrop)
    cano = measure(path, ccrop)
    rows.append({
        "name": name,
        "path": path,
        "whole_mean": whole["mean_abs"],
        "whole_rmse": whole["rmse"],
        "terrain_mean": terr["mean_abs"],
        "terrain_rmse": terr["rmse"],
        "canopy_mean": cano["mean_abs"],
        "canopy_rmse": cano["rmse"],
        "frac_diff_terrain": terr.get("fraction_differ", 0.0),
    })
    if name == "default":
        subprocess.check_call(
            ["python", diff, golden, path, "--crop", "none",
             "--out", os.path.join(report_dir, "heatmap")],
            stdout=subprocess.DEVNULL,
        )

# pretty table
print(f"{'variant':<12} {'whole':>7} {'terrain':>8} {'canopy':>8}  note")
print("-" * 52)
base_t = next(r["terrain_mean"] for r in rows if r["name"] == "default")
for r in rows:
    d = r["terrain_mean"] - base_t
    note = "BASELINE" if r["name"] == "default" else f"Δterr {d:+.2f}"
    print(f"{r['name']:<12} {r['whole_mean']:7.2f} {r['terrain_mean']:8.2f} {r['canopy_mean']:8.2f}  {note}")

base = next(r for r in rows if r["name"] == "default")
print()
print(f"REGRESS gate: whole<{reg_w} terrain_crop<{reg_c}  (fail = regression past current floor)")
print(f"TARGET  gate: terrain_crop≤{tgt_c}  (HARD_SCENE_TARGET={hard_tgt})")

reg_ok = base["whole_mean"] < reg_w and base["terrain_mean"] < reg_c
tgt_ok = base["terrain_mean"] <= tgt_c

print(f"baseline whole={base['whole_mean']:.2f} terrain={base['terrain_mean']:.2f} canopy={base['canopy_mean']:.2f}")
print(f"REGRESS  {'PASS' if reg_ok else 'FAIL'}")
print(f"TARGET   {'MET' if tgt_ok else 'MISS'} (crop {base['terrain_mean']:.2f} vs ≤{tgt_c})")

# winner ablation (lowest terrain crop)
best = min(rows, key=lambda r: r["terrain_mean"])
if best["name"] != "default":
    print(f"best ablation: {best['name']} terrain={best['terrain_mean']:.2f} "
          f"(baseline {base_t:.2f}, Δ {best['terrain_mean']-base_t:+.2f})")
else:
    print("best ablation: default (no knobs beat baseline)")

summary = {
    "scene": "seed0_leaf_canopy",
    "baseline": base,
    "variants": rows,
    "regress_ok": reg_ok,
    "target_ok": tgt_ok,
    "gates": {
        "regress_whole": reg_w,
        "regress_crop": reg_c,
        "target_crop": tgt_c,
        "hard_scene_target": hard_tgt,
    },
}
sum_path = Path(report_dir) / "summary.json"
sum_path.write_text(json.dumps(summary, indent=2))
print(f"wrote {sum_path}")
print(f"heatmap under {report_dir}/heatmap/")

if not reg_ok:
    print("HARD_SCENE FAIL: baseline regressed past floor (see VERIFY.md)")
    sys.exit(1)
if hard_tgt and not tgt_ok:
    print(f"HARD_SCENE FAIL: TARGET crop≤{tgt_c} not met (HARD_SCENE_TARGET=1)")
    sys.exit(1)
print("HARD_SCENE PASS (regress gate; target is aspirational until HARD_SCENE_TARGET=1)")
sys.exit(0)
PY
GATE=$?

# also stash baseline next to mc_capture for eyeballing
cp -f "$REPORT_DIR/cand_default.png" "$OUT/hard_scene_magma.png" 2>/dev/null || true

echo
echo "artifacts:"
echo "  golden   : $GOLDEN"
echo "  baseline : $REPORT_DIR/cand_default.png"
echo "  summary  : $REPORT_DIR/summary.json"
echo "  heatmap  : $REPORT_DIR/heatmap/"
exit $GATE
