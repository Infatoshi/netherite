#!/usr/bin/env bash
# run_rung4.sh - the repeatable rung-4 check: render the seed-0 ChunkScene through
# magma at the MC client resolution/pose (rung4_candidate) and whole-frame
# pixel-diff it against the captured REAL Minecraft golden (mc_frame.png), both
# whole-frame and over a terrain crop.
#
# This does NOT capture the MC frame (that is capture.sh, which needs the live
# game + display :1). It consumes whatever mc_frame.png is present - a real golden
# from a prior capture.sh run, or the committed documented placeholder. Diffing
# against a placeholder still exercises the whole harness end to end; the numbers
# are only MEANINGFUL once mc_frame.png is a real capture at the matching pose.
#
# Rung 4 is a MEAN-channel gate vs live MC, not bitwise. It is intentionally
# HARSH relative to the historical 90/70 "not completely broken" floor: tols sit
# just above measured (~46 whole / ~39 crop) so lighting/atlas regressions fail.
# Structural bugs (lily as full cube in swamps) are gated by make test-model-oracle
# / test-mesh / test-jar-models - seed-0 does not show them. See VERIFY.md.
set -euo pipefail
cd "$(dirname "$0")/../../.."          # -> c/magma

MCSIM="$(cd ../mc-sim/core && pwd)"
DIFF="$(cd ../render-opt/wholeframe && pwd)/diff_frame.py"
OUT=raster/verify/mc_capture
GOLDEN="$OUT/mc_frame.png"
CAND_PPM=/tmp/rung4_candidate.ppm
CAND_PNG="$OUT/magma_frame.png"
FLAGS=(-O2 -ffp-contract=off -Wall -Icore -I. -I"$MCSIM")

echo "== build objects =="
for u in world/mesh_mc world/light world/populate_mc assets/blockmodels \
         renderkernels/rk_31_facebakery_make_quad game/caps \
         core/math core/shade cpu/raster_cpu transform; do
  gcc "${FLAGS[@]}" -c "$u.c" -o "$u.o"
done

echo "== build + render candidate (854x480, matched pose) =="
gcc "${FLAGS[@]}" raster/verify/mc_capture/rung4_candidate.c \
    world/mesh_mc.o world/light.o world/populate_mc.o assets/blockmodels.o \
    renderkernels/rk_31_facebakery_make_quad.o game/caps.o core/math.o \
    core/shade.o cpu/raster_cpu.o transform.o \
    -o /tmp/rung4_candidate -lm
/tmp/rung4_candidate "$CAND_PPM"

# Save a PNG copy of the magma frame next to the golden for eyeballing.
uv run --no-project --with pillow python - "$CAND_PPM" "$CAND_PNG" <<'PY'
import sys; from PIL import Image
Image.open(sys.argv[1]).convert("RGB").save(sys.argv[2])
print("wrote", sys.argv[2])
PY

if [ ! -s "$GOLDEN" ]; then
  echo "ERROR: no golden at $GOLDEN (run capture.sh first)"; exit 1
fi

# Terrain crop: lower-center band where BOTH magma and MC show ground (excludes
# the pure-sky top quarter and the left/right sky-void wedges of magma's island).
TCROP="180:479,180:674"

echo
echo "== whole-frame diff (magma vs MC golden) =="
uv run --no-project --with numpy --with pillow python "$DIFF" \
    "$GOLDEN" "$CAND_PNG" --crop none --out /tmp/rung4_diff

echo "== terrain-crop diff ($TCROP) =="
uv run --no-project --with numpy --with pillow python "$DIFF" \
    "$GOLDEN" "$CAND_PNG" --crop "$TCROP"

# --- TIGHT tolerance gate (ratchet down as lighting/atlas/sky improve) ------
# Measured (seed 0, view-distance mesh, 2026-07-09): whole ~31/ch, crop ~27/ch.
# Old gate WHOLE=90 CROP=70 only failed total pose breakage / placeholder goldens.
# Gate sits ~7 above measured so small regressions fail. Target over time:
# crop <15, then <5 (see VERIFY.md). Override with WHOLE_TOL=/CROP_TOL= env.
WHOLE_TOL="${WHOLE_TOL:-38.0}"
CROP_TOL="${CROP_TOL:-33.0}"
echo
echo "== tight tolerance gate (whole<$WHOLE_TOL, crop<$CROP_TOL) =="
uv run --no-project --with numpy --with pillow python - \
    "$DIFF" "$GOLDEN" "$CAND_PNG" "$TCROP" "$WHOLE_TOL" "$CROP_TOL" <<'PY'
import json, subprocess, sys
diff, golden, cand, tcrop, whole_tol, crop_tol = sys.argv[1:7]
def run(crop):
    out = subprocess.check_output(["python", diff, golden, cand, "--crop", crop, "--json"])
    j = json.loads(out)
    return j["comparisons"][0]
whole = run("none")["whole"]
crop  = run(tcrop)["crop"]
wm, cm = whole["mean_abs"], crop["mean_abs"]
ok = wm < float(whole_tol) and cm < float(crop_tol)
print("whole mean=%.2f rmse=%.2f | crop mean=%.2f rmse=%.2f"
      % (wm, whole["rmse"], cm, crop["rmse"]))
print("RUNG4 %s (whole %.2f<%s , crop %.2f<%s)"
      % ("PASS" if ok else "FAIL", wm, whole_tol, cm, crop_tol))
if not ok:
    print("hint: structural bugs (wrong block models) -> make test-model-oracle test-mesh")
    print("hint: see c/magma/VERIFY.md for the harsh bar")
sys.exit(0 if ok else 1)
PY
GATE=$?

echo
echo "golden : $GOLDEN"
echo "magma: $CAND_PNG"
echo "heatmap: /tmp/rung4_diff/diff_$(basename "$CAND_PNG")"
exit $GATE
