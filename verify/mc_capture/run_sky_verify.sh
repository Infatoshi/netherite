#!/usr/bin/env bash
# run_sky_verify.sh - the SKY rung: render the 4 sky-dominant poses (capture_sky.sh
# goldens mc_sky_<i>.png: eye y=140 above all terrain AND above the y~128 cloud layer,
# frozen clear noon, pitch up -90/-60/-30/-10) through game_candidate and whole-frame
# diff each against the real-MC golden. This isolates the sky gradient + sun from
# terrain: at these poses the frame is (almost) pure RenderGlobal.renderSky output.
# No terrain crop: the whole frame IS the measurement (pose 3 has a thin far-terrain
# band at the bottom; interpret it knowing that).
#
# Expects /tmp/game_candidate already built by run_game_verify.sh (same binary).
set -u
cd "$(dirname "$0")/../../magma"          # -> magma

DIFF="$(cd ../java/render-opt/wholeframe && pwd)/diff_frame.py"
OUT=../verify/mc_capture
FB_W=854
FB_H=480
FOV=70

# mirror capture_sky.sh POSES: "EYE_X EYE_Y EYE_Z MC_YAW MC_PITCH"
POSES=(
  "8.0 140.0 8.0 180 -90"
  "8.0 140.0 8.0 180 -60"
  "8.0 140.0 8.0 180 -30"
  "8.0 140.0 8.0 180 -10"
)

[ -x /tmp/game_candidate ] || { echo "FAIL: /tmp/game_candidate missing (run run_game_verify.sh first)"; exit 1; }

NP=${#POSES[@]}
for ((i=0; i<NP; i++)); do
  read -r EX EY EZ MYAW MPITCH <<<"${POSES[$i]}"
  CYAW=$(python3 -c "print(180.0 - $MYAW)")
  CPITCH=$(python3 -c "print(-($MPITCH))")
  /tmp/game_candidate --eye "$EX" "$EY" "$EZ" --yaw "$CYAW" --pitch "$CPITCH" \
      --fov "$FOV" --w "$FB_W" --h "$FB_H" --ppm "/tmp/magma_sky_$i.ppm" >/dev/null
  python3 -c "
import subprocess, json, sys
out = subprocess.check_output(['python3', '$DIFF', '$OUT/mc_sky_$i.png', '/tmp/magma_sky_$i.ppm', '--json'])
e = json.loads(out)['comparisons'][0]['whole']
print('sky pose $i (pitch $MPITCH): whole mean %.2f rmse %.2f' % (e['mean_abs'], e['rmse']))
"
done
