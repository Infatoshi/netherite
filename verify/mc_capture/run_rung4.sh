#!/usr/bin/env bash
# run_rung4.sh - the repeatable rung-4 check: render the seed-0 hard leaf-canopy
# scene through magma at the MC client resolution/pose (rung4_candidate) and
# whole-frame pixel-diff it against the captured REAL Minecraft golden
# (mc_frame.png), both whole-frame and over a terrain crop.
#
# This does NOT capture the MC frame (that is capture.sh, which needs the live
# game + display :1). It consumes whatever mc_frame.png is present - a real golden
# from a prior capture.sh run, or the committed documented placeholder. Diffing
# against a placeholder still exercises the whole harness end to end; the numbers
# are only MEANINGFUL once mc_frame.png is a real capture at the matching pose.
#
# Pose + pipeline match hard-scene-verify / multi-verify seed0 (camera.json):
#   eye (8.3, 95, 40.5), FOV 77, pose_scene prepare, sky, terrain fog.
# The older FOV-70 / z=40.0 / chunk_scene path is stale against the re-capture
# (run_multi_verify.sh says the same); scoring it against this golden measured
# the pose gap, not the renderer, which is where the old ~42 whole came from.
#
# COVERAGE NOTE: with the pose corrected this is the LEAN TWIN of
# hard-scene-verify - same scene, same golden, same numbers (1.13 / 0.67) -
# through a standalone fixed-pose binary instead of game_candidate's
# arbitrary-pose path with ablations. It is a second entry point over
# mesh/light/populate/shade/raster, NOT independent scene coverage.
#
# Rung 4 is a MEAN-channel gate vs live MC, not bitwise. Tols sit just above
# measured (1.13 whole / 0.67 crop at the corrected pose) so lighting/atlas
# regressions fail. The old 38/33 pair was sized for the stale-pose numbers
# (~42/~33) and could no longer fail anything once the pose was fixed.
# Structural bugs (lily as full cube in swamps) are gated by make test-model-oracle
# / test-mesh / test-jar-models - seed-0 does not show them. See VERIFY.md.
set -euo pipefail
cd "$(dirname "$0")/../../magma"          # -> magma

BLAZE="$(cd ../blaze/core && pwd)"
DIFF="$(cd ../java/render-opt/wholeframe && pwd)/diff_frame.py"
OUT=../verify/mc_capture
GOLDEN="$OUT/mc_frame.png"
CAND_PPM=/tmp/rung4_candidate.ppm
CAND_PNG="$OUT/magma_frame.png"
FLAGS=(-O2 -ffp-contract=off -Wall -Icore -I. -I"$BLAZE")

# Same cumulative populate order as hard-scene-verify (spawn qrl_0 -> chunk 2,11).
export MAGMA_SPAWN_CX="${MAGMA_SPAWN_CX:-2}"
export MAGMA_SPAWN_CZ="${MAGMA_SPAWN_CZ:-11}"
echo "== build objects =="
for u in world/mesh_mc world/light world/populate_mc assets/blockmodels \
         renderkernels/rk_31_facebakery_make_quad game/sky game/caps \
         core/math core/shade cpu/raster_cpu transform; do
  gcc "${FLAGS[@]}" -c "$u.c" -o "$u.o"
done

echo "== build + render candidate (854x480, hard-scene pose) =="
echo "    prepare: spawn=($MAGMA_SPAWN_CX,$MAGMA_SPAWN_CZ) prep_list=${MAGMA_PREP_LIST:-derived}"
gcc "${FLAGS[@]}" ../verify/mc_capture/rung4_candidate.c \
    world/mesh_mc.o world/light.o world/populate_mc.o assets/blockmodels.o \
    renderkernels/rk_31_facebakery_make_quad.o game/sky.o game/caps.o core/config.o core/math.o \
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
# Measured at the corrected hard-scene pose (seed 0, 2026-07-25):
# whole 1.13/ch, crop 0.67/ch. Gate sits ~0.4 above measured so a small
# lighting/atlas/sky regression fails. Target over time: crop <0.3, then
# bitwise (see VERIFY.md). Override with WHOLE_TOL=/CROP_TOL= env.
WHOLE_TOL="${WHOLE_TOL:-1.5}"
CROP_TOL="${CROP_TOL:-1.0}"
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
    print("hint: see magma/VERIFY.md for the harsh bar")
sys.exit(0 if ok else 1)
PY
GATE=$?

echo
echo "golden : $GOLDEN"
echo "magma: $CAND_PNG"
echo "heatmap: /tmp/rung4_diff/diff_$(basename "$CAND_PNG")"
exit $GATE
