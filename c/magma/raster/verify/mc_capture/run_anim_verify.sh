#!/usr/bin/env bash
# Replay the fixed animation tape twice through magma's CPU renderer, require
# bitwise determinism, then gate fixed water/portal/underwater regions.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../../../.." && pwd)"
TRACE="$ROOT/c/magma/raster/verify/trace"
FIXTURE="$HERE/anim_fixture"
OUT="$HERE/anim_out"
TAPE="${ANIM_TAPE:-}"

if [ -z "$TAPE" ]; then
	shopt -s nullglob
	tapes=("$FIXTURE"/*.jsonl)
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
	# A forced fixed-pose capture can have a physics-only packet discrepancy.
	# Pixel output is still authoritative; rc 4 is reported but not hidden.
	if [ "$rc" -ne 0 ] && [ "$rc" -ne 4 ]; then
		return "$rc"
	fi
	echo "anim replay $(basename "$out"): rc=$rc"
}

replay "$OUT/run1"
replay "$OUT/run2"

cmp "$OUT/run1/magma_frames.npy" "$OUT/run2/magma_frames.npy"
cmp "$OUT/run1/magma_frames.ticks.npy" "$OUT/run2/magma_frames.ticks.npy"
echo "magma deterministic rerun: PASS (frames and tick index bitwise identical)"

uv run --no-project --with numpy --with pillow python "$HERE/anim_verify.py" \
	--tape "$TAPE" \
	--magma "$OUT/run1/magma_frames.npy" \
	--ticks "$OUT/run1/magma_frames.ticks.npy" \
	--scene "$HERE/anim_scene.json" \
	--out "$OUT/evidence"
