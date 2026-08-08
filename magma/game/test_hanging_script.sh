#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
mkdir -p .tmp
SCRIPT=".tmp/test_hanging_script.$$.jsonl"
STATE=".tmp/test_hanging_script_state.$$.jsonl"
MAP_NAME="test_hanging_script_map.$$.u8"
MAP=".tmp/$MAP_NAME"
trap 'rm -f "$SCRIPT" "$STATE" "$MAP"' EXIT
dd if=/dev/zero of="$MAP" bs=16384 count=1 status=none
printf '%s\n' \
    '{"tick":0,"type":"set_block","x":8,"y":100,"z":9,"id":1,"meta":0}' \
    '{"tick":0,"type":"set_block","x":12,"y":100,"z":12,"id":85,"meta":0}' \
    '{"tick":0,"type":"set_block","x":16,"y":100,"z":17,"id":1,"meta":0}' \
    '{"tick":0,"type":"set_painting","dim":0,"eid":7501,"hanging_x":8,"hanging_y":100,"hanging_z":8,"facing":2,"art":0,"tick_counter":11,"most":-1070935975390360081,"least":-9141386507638288913}' \
    '{"tick":0,"type":"set_leash_knot","dim":0,"eid":7502,"x":12,"y":100,"z":12,"tick_counter":22,"most":8152436061464415727,"least":7008726920094100975}' \
    '{"tick":0,"type":"set_item_frame","dim":0,"eid":7503,"hanging_x":16,"hanging_y":100,"hanging_z":16,"facing":2,"item":358,"count":1,"meta":42,"rotation":0,"tick_counter":0,"item_drop_chance":1.0,"entity_seed48":20015998343868,"entity_have_gaussian":0,"entity_gaussian":0.0,"most":3,"least":4,"tracker_update_counter":0,"map_data_present":1,"map_dimension":0,"map_x_center":0,"map_z_center":0,"map_scale":0,"map_tracking_position":1,"map_unlimited_tracking":0,"map_decoration_present":1,"map_decoration_type":1,"map_decoration_x":32,"map_decoration_z":32,"map_decoration_rotation":8,"map_colors_file":"'"$MAP_NAME"'"}' \
    >"$SCRIPT"
MAGMA_CAPSULE_DIR="$ROOT/.tmp" \
./magma_game --world superflat --headless --ticks 1 --mobs off \
    --script "$SCRIPT" --state-out "$STATE" --render off --pace unlimited
UV_CACHE_DIR="${UV_CACHE_DIR:-$HOME/.cache/uv}" \
TMPDIR="${TMPDIR:-$ROOT/../.tmp}" \
uv run --no-project python - "$STATE" <<'PY'
import json
import sys

state = json.loads(open(sys.argv[1], encoding="utf-8").readline())
assert state["paintings"] == [{
    "eid": 7501, "x": 8.5, "y": 100.5, "z": 8.96875,
    "hanging_x": 8, "hanging_y": 100, "hanging_z": 8,
    "facing": 2, "art": 0, "tick_counter": 12,
    "loaded_order": -1,
    "uuid_most": -1070935975390360081,
    "uuid_least": -9141386507638288913,
}], state["paintings"]
assert state["leash_knots"] == [{
    "eid": 7502, "x": 12.5, "y": 100.5, "z": 12.5,
    "hanging_x": 12, "hanging_y": 100, "hanging_z": 12,
    "tick_counter": 23,
    "loaded_order": -1,
    "uuid_most": 8152436061464415727,
    "uuid_least": 7008726920094100975,
}], state["leash_knots"]
assert len(state["item_frames"]) == 1, state["item_frames"]
frame = state["item_frames"][0]
assert frame["eid"] == 7503 and frame["item"] == 358
assert frame["map_data_present"] and frame["map_decoration_present"]
assert frame["map_decoration_x"] == 32 and frame["map_decoration_z"] == 32
PY
echo "hanging script: PASS"
