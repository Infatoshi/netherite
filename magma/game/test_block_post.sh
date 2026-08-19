#!/usr/bin/env bash
# Focused set_block_post contract: post-tick removal is visible in state t and
# frees next-tick collision. Not part of the full script suite by default.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
make game

STATE_SOLID=/tmp/magma-block-post-solid-state.jsonl
STATE_AIR=/tmp/magma-block-post-air-state.jsonl
SCRIPT_SOLID=/tmp/magma-block-post-solid.jsonl
SCRIPT_AIR=/tmp/magma-block-post-air.jsonl
SCRIPT_WALK=/tmp/magma-block-post-walk.jsonl
STATE_WALK=/tmp/magma-block-post-walk-state.jsonl
SCRIPT_WALK_SOLID=/tmp/magma-block-post-walk-solid.jsonl
STATE_WALK_SOLID=/tmp/magma-block-post-walk-solid-state.jsonl

# Superflat feet at y=4; place a full-height stone wall at x=9 (body + eye).
# Without set_block_post the wall remains in state t=0 nearby volume + look.
printf '%s\n' \
	'{"tick":0,"type":"set_pose","x":8.5,"y":4,"z":8.5,"yaw":-90,"pitch":0}' \
	'{"tick":0,"type":"set_block","x":9,"y":4,"z":8,"id":1,"meta":0}' \
	'{"tick":0,"type":"set_block","x":9,"y":5,"z":8,"id":1,"meta":0}' \
	>"$SCRIPT_SOLID"
./magma_game --world superflat --headless --ticks 1 --script "$SCRIPT_SOLID" \
	--state-out "$STATE_SOLID" --render off --pace unlimited

# Same setup, but recorded post-tick finals are air: after action t simulates,
# reanchor lands before state capture.
printf '%s\n' \
	'{"tick":0,"type":"set_pose","x":8.5,"y":4,"z":8.5,"yaw":-90,"pitch":0}' \
	'{"tick":0,"type":"set_block","x":9,"y":4,"z":8,"id":1,"meta":0}' \
	'{"tick":0,"type":"set_block","x":9,"y":5,"z":8,"id":1,"meta":0}' \
	'{"tick":0,"type":"set_block_post","x":9,"y":4,"z":8,"id":0,"meta":0}' \
	'{"tick":0,"type":"set_block_post","x":9,"y":5,"z":8,"id":0,"meta":0}' \
	>"$SCRIPT_AIR"
./magma_game --world superflat --headless --ticks 1 --script "$SCRIPT_AIR" \
	--state-out "$STATE_AIR" --render off --pace unlimited

uv run --no-project python - <<'PY'
import json
solid = json.loads(open("/tmp/magma-block-post-solid-state.jsonl", encoding="utf-8").read())
air = json.loads(open("/tmp/magma-block-post-air-state.jsonl", encoding="utf-8").read())
assert solid["tick"] == air["tick"] == 1
assert solid["nearby_hash"] != air["nearby_hash"], (
    "set_block_post air must change state-t world digest vs solid left in place",
    solid["nearby_hash"], air["nearby_hash"])
# look ray along +x (yaw -90) should see stone when solid remains
look = solid.get("look")
assert look is not None and look.get("id") == 1, look
assert (look.get("x"), look.get("z")) == (9, 8), look
# after post-tick air reanchor the ray must not report stone
look_a = air.get("look")
assert look_a is None or look_a.get("id") != 1, look_a
print("state-t visibility: PASS")
PY

# Next-tick collision: wall stays solid without post => walk stalls.
# With set_block_post air on t=0, forward motion on t=1+ clears past x=9.
: >"$SCRIPT_WALK_SOLID"
printf '%s\n' \
	'{"tick":0,"type":"set_pose","x":8.5,"y":4,"z":8.5,"yaw":-90,"pitch":0}' \
	'{"tick":0,"type":"set_block","x":9,"y":4,"z":8,"id":1,"meta":0}' \
	'{"tick":0,"type":"set_block","x":9,"y":5,"z":8,"id":1,"meta":0}' \
	>>"$SCRIPT_WALK_SOLID"
for t in $(seq 1 15); do
	printf '{"tick":%d,"type":"action","forward":1}\n' "$t" >>"$SCRIPT_WALK_SOLID"
done
./magma_game --world superflat --headless --ticks 16 --script "$SCRIPT_WALK_SOLID" \
	--state-out "$STATE_WALK_SOLID" --render off --pace unlimited

: >"$SCRIPT_WALK"
printf '%s\n' \
	'{"tick":0,"type":"set_pose","x":8.5,"y":4,"z":8.5,"yaw":-90,"pitch":0}' \
	'{"tick":0,"type":"set_block","x":9,"y":4,"z":8,"id":1,"meta":0}' \
	'{"tick":0,"type":"set_block","x":9,"y":5,"z":8,"id":1,"meta":0}' \
	'{"tick":0,"type":"set_block_post","x":9,"y":4,"z":8,"id":0,"meta":0}' \
	'{"tick":0,"type":"set_block_post","x":9,"y":5,"z":8,"id":0,"meta":0}' \
	>>"$SCRIPT_WALK"
for t in $(seq 1 15); do
	printf '{"tick":%d,"type":"action","forward":1}\n' "$t" >>"$SCRIPT_WALK"
done
./magma_game --world superflat --headless --ticks 16 --script "$SCRIPT_WALK" \
	--state-out "$STATE_WALK" --render off --pace unlimited

uv run --no-project python - <<'PY'
import json
rows_s = [json.loads(l) for l in open(
    "/tmp/magma-block-post-walk-solid-state.jsonl", encoding="utf-8")]
rows_a = [json.loads(l) for l in open(
    "/tmp/magma-block-post-walk-state.jsonl", encoding="utf-8")]
assert len(rows_s) == len(rows_a) == 16
# Solid wall: player must remain west of the block face (x < 9).
assert rows_s[-1]["x"] < 9.0, ("solid stalled x", rows_s[-1]["x"])
# Air reanchor: player walks through the former wall cell.
assert rows_a[-1]["x"] > 9.5, ("air walked x", rows_a[-1]["x"])
assert rows_a[-1]["x"] > rows_s[-1]["x"] + 0.5
print("next-tick collision: PASS")
PY

# Fail closed: unknown field / out-of-range meta both reject (rc!=0).
printf '%s\n' \
	'{"tick":0,"type":"set_block_post","x":0,"y":70,"z":0,"id":1,"meta":0,"extra":1}' \
	>/tmp/magma-block-post-bad-field.jsonl
if ./magma_game --world superflat --headless --ticks 1 \
	--script /tmp/magma-block-post-bad-field.jsonl \
	--render off --pace unlimited >/tmp/magma-block-post-bad-field.out 2>&1; then
	echo "unknown field on set_block_post unexpectedly succeeded" >&2
	exit 1
fi
rg -q 'invalid set_block_post' /tmp/magma-block-post-bad-field.out

printf '%s\n' \
	'{"tick":0,"type":"set_block_post","x":0,"y":70,"z":0,"id":1,"meta":16}' \
	>/tmp/magma-block-post-bad-meta.jsonl
if ./magma_game --world superflat --headless --ticks 1 \
	--script /tmp/magma-block-post-bad-meta.jsonl \
	--render off --pace unlimited >/tmp/magma-block-post-bad-meta.out 2>&1; then
	echo "out-of-range meta on set_block_post unexpectedly succeeded" >&2
	exit 1
fi
rg -q 'invalid set_block_post' /tmp/magma-block-post-bad-meta.out

echo "set_block_post route: PASS"
