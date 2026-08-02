#!/usr/bin/env bash
# run_multi_verify.sh - multi-scene pixel gate (seed0 + optional seed7 swamp).
# Target crop <=15/ch (see VERIFY.md). Gate uses CROP_TOL just above measured.
set -euo pipefail
cd "$(dirname "$0")/../../magma"

BLAZE="$(cd ../blaze/core && pwd)"
DIFF="$(cd ../java/render-opt/wholeframe && pwd)/diff_frame.py"
OUT=../verify/mc_capture
FLAGS=(-O2 -ffp-contract=off -Wall -Icore -I. -I"$BLAZE")
TCROP="180:479,180:674"
# Ratchet at the measured instrumented seed-0 pose. Override to force tighter.
WHOLE_TOL="${WHOLE_TOL:-35.0}"
CROP_TOL="${CROP_TOL:-15.0}"

# Keep the seed-0 duplicate on the same recorded camera and world preparation as
# run_hard_scene.sh. The old hand-written z=40.0/FOV=70 pose was stale after the
# live capture established z=40.5/FOV=77.
SEED0_EYE_X=8.3
SEED0_EYE_Y=95.0
SEED0_EYE_Z=40.5
SEED0_YAW=0
SEED0_PITCH=-35
SEED0_FOV=77
if [ -s "$OUT/camera.json" ]; then
	SEED0_EYE_X=$(jq -r '.eye_x // 8.3' "$OUT/camera.json")
	SEED0_EYE_Y=$(jq -r '.eye_y // 95.0' "$OUT/camera.json")
	SEED0_EYE_Z=$(jq -r '.eye_z // 40.5' "$OUT/camera.json")
	SEED0_YAW=$(jq -r '.magma_yaw_deg // 0.0' "$OUT/camera.json")
	SEED0_PITCH=$(jq -r '.magma_pitch_deg // -35.0' "$OUT/camera.json")
	SEED0_FOV=$(jq -r '.fov_effective // 77.0' "$OUT/camera.json")
fi
SEED0_SPAWN_CX="${MAGMA_SPAWN_CX:-2}"
SEED0_SPAWN_CZ="${MAGMA_SPAWN_CZ:-11}"
SEED0_PREP_LIST="${MAGMA_PREP_LIST:-}"
if [ -z "$SEED0_PREP_LIST" ]; then
	for candidate in \
		/tmp/frustum_wv2/wv_prep_list.txt \
		trace/out/wv_prep_list.txt; do
		if [ -s "$candidate" ]; then
			SEED0_PREP_LIST="$candidate"
			break
		fi
	done
fi

# Seed-7 must be driven by its own instrumented sidecar.  The previous fixture
# hardcoded a guessed FOV/pose and could not distinguish a renderer regression
# from looking at a different part of the world.
SEED7_EYE_X=16.5
SEED7_EYE_Y=89.0
SEED7_EYE_Z=268.5
SEED7_YAW=0
SEED7_PITCH=-40
SEED7_FOV=77
SEED7_SPAWN_CX=12
SEED7_SPAWN_CZ=15
if [ -s "$OUT/camera_seed7.json" ]; then
	SEED7_EYE_X=$(jq -r '.eye_x' "$OUT/camera_seed7.json")
	SEED7_EYE_Y=$(jq -r '.eye_y' "$OUT/camera_seed7.json")
	SEED7_EYE_Z=$(jq -r '.eye_z' "$OUT/camera_seed7.json")
	SEED7_YAW=$(jq -r '.magma_yaw_deg' "$OUT/camera_seed7.json")
	SEED7_PITCH=$(jq -r '.magma_pitch_deg' "$OUT/camera_seed7.json")
	SEED7_FOV=$(jq -r '.fov_effective' "$OUT/camera_seed7.json")
	SEED7_SPAWN_CX=$(jq -r '.world.spawn_cx' "$OUT/camera_seed7.json")
	SEED7_SPAWN_CZ=$(jq -r '.world.spawn_cz' "$OUT/camera_seed7.json")
fi

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
	-o /tmp/game_candidate_multi -lm

measure() {
	local name="$1" seed="$2" ex="$3" ey="$4" ez="$5" cyaw="$6" cpitch="$7"
	local fov="$8" golden="$9"
	local cand_ppm="/tmp/cand_${name}.ppm"
	local cand_png="${OUT}/magma_${name}.png"
	local -a render_env=()
	if [ ! -s "$golden" ]; then
		echo "$name SKIP (missing $golden)"
		return 2
	fi
	if [ "$name" = seed0 ]; then
		render_env+=("MAGMA_SPAWN_CX=$SEED0_SPAWN_CX")
		render_env+=("MAGMA_SPAWN_CZ=$SEED0_SPAWN_CZ")
		if [ -n "$SEED0_PREP_LIST" ]; then
			render_env+=("MAGMA_PREP_LIST=$SEED0_PREP_LIST")
		fi
	elif [ "$name" = seed7 ]; then
		render_env+=("MAGMA_SPAWN_CX=$SEED7_SPAWN_CX")
		render_env+=("MAGMA_SPAWN_CZ=$SEED7_SPAWN_CZ")
	fi
	env "${render_env[@]}" /tmp/game_candidate_multi \
		--seed "$seed" --eye "$ex" "$ey" "$ez" \
		--yaw "$cyaw" --pitch "$cpitch" --fov "$fov" --w 854 --h 480 \
		--ppm "$cand_ppm" >"/tmp/cand_${name}.log" 2>&1
	uv run --no-project --with pillow python -c \
		"from PIL import Image; Image.open('$cand_ppm').convert('RGB').save('$cand_png')"
	uv run --no-project --with numpy --with pillow python - \
		"$DIFF" "$golden" "$cand_png" "$TCROP" "$WHOLE_TOL" "$CROP_TOL" "$name" <<'PY'
import json, subprocess, sys
diff, golden, cand, tcrop, wt, ct, name = sys.argv[1:8]
def run(crop):
    out = subprocess.check_output(["python", diff, golden, cand, "--crop", crop, "--json"])
    return json.loads(out)["comparisons"][0]
w = run("none")["whole"]["mean_abs"]
c = run(tcrop)["crop"]["mean_abs"]
ok = w < float(wt) and c < float(ct)
print("%-8s whole=%6.2f crop=%6.2f  %s" % (name, w, c, "PASS" if ok else "FAIL"))
sys.exit(0 if ok else 1)
PY
}

npass=0
nfail=0
nskip=0
echo
echo "MULTI-SCENE gate whole<$WHOLE_TOL crop<$CROP_TOL"
# seed0 rung4 pose
if measure seed0 0 "$SEED0_EYE_X" "$SEED0_EYE_Y" "$SEED0_EYE_Z" \
	"$SEED0_YAW" "$SEED0_PITCH" "$SEED0_FOV" "${OUT}/mc_frame.png"; then
	npass=$((npass + 1))
else
	st=$?
	if [ "$st" = 2 ]; then nskip=$((nskip + 1)); else nfail=$((nfail + 1)); fi
fi
# Seed-7 swamp aerial is a required second scene.  Both the PNG and its
# authoritative camera sidecar must be present, and a mismatch fails the gate.
if [ -s "${OUT}/mc_seed7.png" ] && [ -s "${OUT}/camera_seed7.json" ]; then
	if measure seed7 7 "$SEED7_EYE_X" "$SEED7_EYE_Y" "$SEED7_EYE_Z" \
		"$SEED7_YAW" "$SEED7_PITCH" "$SEED7_FOV" "${OUT}/mc_seed7.png"; then
		npass=$((npass + 1))
	else
		st=$?
		if [ "$st" = 2 ]; then
			nskip=$((nskip + 1))
		else
			nfail=$((nfail + 1))
		fi
	fi
else
	echo "seed7    FAIL (requires mc_seed7.png + camera_seed7.json)"
	nfail=$((nfail + 1))
fi

echo
echo "MULTI_VERIFY pass=$npass fail=$nfail skip=$nskip"
[ "$nfail" -eq 0 ] && [ "$npass" -gt 0 ]
