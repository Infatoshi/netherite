#!/usr/bin/env bash
# Replay the fixed animation tape twice through magma's CPU renderer, require
# bitwise determinism, then gate animated textures and underwater transition.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
TRACE="$ROOT/verify/trace"
FIXTURE="$HERE/anim_fixture"
OUT="$HERE/anim_out"
TAPE="${ANIM_TAPE:-}"

if [ -z "$TAPE" ]; then
	shopt -s nullglob
	tapes=()
	for candidate in "$FIXTURE"/*.jsonl; do
		case "$candidate" in
		*.snapshot_patch.jsonl | *.worldpatch.jsonl | *.geom.jsonl) continue ;;
		esac
		tapes+=("$candidate")
	done
	if [ "${#tapes[@]}" -gt 0 ]; then
		TAPE="${tapes[${#tapes[@]} - 1]}"
	fi
fi
if [ -z "$TAPE" ] || [ ! -s "$TAPE" ]; then
	echo "anim verify: no fixture tape; run $HERE/capture_anim.sh" >&2
	exit 2
fi

mkdir -p "$OUT"
rm -rf "$OUT/run1" "$OUT/run2" "$OUT/evidence"

replay() {
	local out="$1"
	set +e
	uv run --no-project --with numpy --with scipy --with pillow --with nbt \
		python "$TRACE/replay_tape.py" "$TAPE" --cpu --no-gate --out "$out"
	local rc=$?
	set -e
	# The recorder-driven pose anchor must keep this fixture physics-clean.
	if [ "$rc" -ne 0 ]; then
		return "$rc"
	fi
	echo "anim replay $(basename "$out"): rc=$rc"
}

replay "$OUT/run1"
replay "$OUT/run2"

cmp "$OUT/run1/magma_frames.npy" "$OUT/run2/magma_frames.npy"
cmp "$OUT/run1/magma_frames.ticks.npy" "$OUT/run2/magma_frames.ticks.npy"
echo "magma deterministic rerun: PASS (frames and tick index bitwise identical)"

read -r total_time portal_frame frame_count < <(
	uv run --no-project python - "$TAPE" <<'PY'
import json
import sys

rows = [json.loads(line) for line in open(sys.argv[1]) if line.strip()]
portal = next(row for row in rows[1:] if "portal_frame" in row)
print(rows[0]["total_time"], portal["portal_frame"], len(rows) - 1)
PY
)
cc -std=c11 -O2 -I"$ROOT/magma" \
	"$HERE/anim_atlas_dump.c" "$ROOT/magma/assets/blockmodels.c" -lm \
	-o "$OUT/anim_atlas_dump"
"$OUT/anim_atlas_dump" "$total_time" "$portal_frame" "$frame_count" \
	"$OUT/atlas_frames.rgba"

uv run --no-project --with numpy --with pillow python "$HERE/anim_verify.py" \
	--tape "$TAPE" \
	--magma "$OUT/run1/magma_frames.npy" \
	--ticks "$OUT/run1/magma_frames.ticks.npy" \
	--atlas "$OUT/atlas_frames.rgba" \
	--scene "$HERE/anim_scene.json" \
	--out "$OUT/evidence"
