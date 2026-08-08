#!/usr/bin/env bash
# run_game_verify.sh - the HEADLINE arbitrary-pose check: for each pose in the
# POSES list, render the seed-0 view-distance world through magma (game_candidate
# at that pose) and whole-frame pixel-diff it against the REAL Minecraft golden
# captured at the SAME pose (mc_pose_<i>.png from capture_poses.sh). Prints a
# per-pose table (whole + terrain-crop mean/rmse, PASS/FAIL vs the coarse
# tolerance) and an aggregate.
#
# This does NOT capture the MC frames (that is capture_poses.sh, which needs the
# live game on display :1). It consumes whatever mc_pose_<i>.png are present. If a
# pose has no captured golden, it falls back to mc_frame.png for pose 0 (the
# existing rung-4 golden) and reports the pose as PIPELINE-VALIDATED-ONLY otherwise.
#
# POSES here MUST mirror capture_poses.sh's POSES (same magma EYE + MC yaw/pitch)
# so each magma render registers to its golden. The magma camera convention is
# derived from the MC pose: magma_yaw = 180 - MC_yaw, magma_pitch = -MC_pitch
# (see capture.sh / game_candidate.c). Pose 0 is the frozen rung-4 pose, so its
# numbers reproduce run_rung4.sh (whole mean ~46/ch, crop ~39/ch), proving the
# arbitrary-pose renderer + diff wiring is correct.
#
# Rung is a COARSE whole-frame tolerance, NOT a tight match: MC also draws a real
# sky gradient + clouds, smooth per-vertex lighting, a runtime-stitched atlas, and
# full view-distance terrain. Interpret the numbers; the terrain crop is honest.
set -euo pipefail
cd "$(dirname "$0")/../../magma"          # -> magma

BLAZE="$(cd ../blaze/core && pwd)"
DIFF="$(cd ../java/render-opt/wholeframe && pwd)/diff_frame.py"
OUT=../verify/mc_capture
FLAGS=(-O2 -ffp-contract=off -Wall -Icore -I. -I"$BLAZE")

# --- POSE LIST: "EYE_X EYE_Y EYE_Z MC_YAW MC_PITCH" (mirror capture_poses.sh) ---
POSES=(
  "8.2994 95.0 40.0 180 35"
  "8.0    82.0 24.0 180 10"
  "0.0    88.0  8.0  90 20"
  "8.0   112.0 48.0 180 55"
)

FB_W=854
FB_H=480
FOV=70
# Lower-center terrain band where magma + MC both show ground (excludes the sky
# top and the sky-void wedges). Coarse honest number; same crop across poses.
TCROP="180:479,180:674"
# Ratcheted mean gate (see VERIFY.md / run_rung4.sh). Override via env.
WHOLE_TOL="${WHOLE_TOL:-38.0}"
CROP_TOL="${CROP_TOL:-33.0}"

echo "== build objects =="
for u in world/mesh_mc world/light world/populate_mc assets/blockmodels \
         renderkernels/rk_31_facebakery_make_quad game/sky game/caps \
         core/math core/shade cpu/raster_cpu transform; do
  gcc "${FLAGS[@]}" -c "$u.c" -o "$u.o" 2>/dev/null
done

echo "== build game_candidate (arbitrary-pose renderer) =="
gcc "${FLAGS[@]}" ../verify/mc_capture/game_candidate.c \
    world/mesh_mc.o world/light.o world/populate_mc.o assets/blockmodels.o \
    renderkernels/rk_31_facebakery_make_quad.o game/sky.o game/caps.o core/config.o core/math.o \
    core/shade.o cpu/raster_cpu.o transform.o \
    -o /tmp/game_candidate -lm

NP=${#POSES[@]}
# per-pose parallel arrays for the final table
declare -a G_GOLDEN G_CAND G_LABEL

echo
for ((i=0; i<NP; i++)); do
  read -r EX EY EZ MYAW MPITCH <<<"${POSES[$i]}"
  # MC -> magma convention
  CYAW=$(python3 -c "print(180.0 - $MYAW)")
  CPITCH=$(python3 -c "print(-1.0 * $MPITCH)")

  # golden: prefer mc_pose_<i>.png; pose 0 falls back to the existing mc_frame.png
  GOLDEN="$OUT/mc_pose_$i.png"
  LABEL="real:mc_pose_$i.png"
  if [ ! -s "$GOLDEN" ]; then
    if [ "$i" = 0 ] && [ -s "$OUT/mc_frame.png" ]; then
      GOLDEN="$OUT/mc_frame.png"; LABEL="real:mc_frame.png(rung4)"
    else
      GOLDEN=""; LABEL="NO-GOLDEN(pipeline-only)"
    fi
  fi

  CAND_PPM="/tmp/game_pose_$i.ppm"
  CAND_PNG="$OUT/magma_pose_$i.png"
  echo "== pose $i: eye($EX,$EY,$EZ) MC yaw=$MYAW pitch=$MPITCH -> magma yaw=$CYAW pitch=$CPITCH =="
  /tmp/game_candidate --eye "$EX" "$EY" "$EZ" --yaw "$CYAW" --pitch "$CPITCH" \
      --fov "$FOV" --w "$FB_W" --h "$FB_H" --ppm "$CAND_PPM" | sed -n '1,3p'
  uv run --no-project --with pillow python - "$CAND_PPM" "$CAND_PNG" <<'PY'
import sys; from PIL import Image
Image.open(sys.argv[1]).convert("RGB").save(sys.argv[2])
PY
  G_GOLDEN[$i]="$GOLDEN"; G_CAND[$i]="$CAND_PNG"; G_LABEL[$i]="$LABEL"
  echo
done

# --- one python pass: per-pose whole+crop diff table + aggregate ---
uv run --no-project --with numpy --with pillow python - \
    "$DIFF" "$TCROP" "$WHOLE_TOL" "$CROP_TOL" \
    "${G_GOLDEN[@]}" "--sep--" "${G_CAND[@]}" "--sep--" "${G_LABEL[@]}" <<'PY'
import json, subprocess, sys
diff, tcrop, whole_tol, crop_tol = sys.argv[1], sys.argv[2], float(sys.argv[3]), float(sys.argv[4])
rest = sys.argv[5:]
a = rest.index("--sep--"); b = rest.index("--sep--", a+1)
goldens = rest[:a]; cands = rest[a+1:b]; labels = rest[b+1:]
n = len(goldens)

def run(golden, cand, crop):
    out = subprocess.check_output(["python", diff, golden, cand, "--crop", crop, "--json"])
    j = json.loads(out)
    e = j["comparisons"][0]
    return e["whole"] if crop == "none" else e["crop"]

print("=" * 92)
print("ARBITRARY-POSE GAME-VERIFY  (coarse tol: whole<%.0f/ch, crop<%.0f/ch)" % (whole_tol, crop_tol))
print("=" * 92)
hdr = "%-4s %-26s %8s %8s %8s %8s  %-6s" % ("pose", "golden", "whole_mn", "whole_rm", "crop_mn", "crop_rm", "verdict")
print(hdr); print("-" * 92)
sum_wm = sum_cm = 0.0; scored = 0; npass = 0
for i in range(n):
    g, c, lab = goldens[i], cands[i], labels[i]
    if not g:
        print("%-4d %-26s %8s %8s %8s %8s  %-6s" % (i, lab, "-", "-", "-", "-", "SKIP"))
        continue
    w = run(g, c, "none"); cr = run(g, c, tcrop)
    wm, wr, cm, cr_ = w["mean_abs"], w["rmse"], cr["mean_abs"], cr["rmse"]
    ok = wm < whole_tol and cm < crop_tol
    npass += 1 if ok else 0
    sum_wm += wm; sum_cm += cm; scored += 1
    print("%-4d %-26s %8.2f %8.2f %8.2f %8.2f  %-6s" %
          (i, lab, wm, wr, cm, cr_, "PASS" if ok else "FAIL"))
print("-" * 92)
if scored:
    print("AGG  %-26s %8.2f %8s %8.2f %8s  %d/%d PASS" %
          ("(mean over %d scored)" % scored, sum_wm/scored, "", sum_cm/scored, "", npass, scored))
else:
    print("AGG  no scored poses (no goldens present) - render pipeline exercised only")
print("=" * 92)
# non-zero exit if any scored pose failed the coarse gate
sys.exit(0 if (scored and npass == scored) else (0 if scored == 0 else 1))
PY
GATE=$?

echo
echo "goldens: $OUT/mc_pose_<i>.png (pose 0 falls back to mc_frame.png)"
echo "magma: $OUT/magma_pose_<i>.png"
exit $GATE
