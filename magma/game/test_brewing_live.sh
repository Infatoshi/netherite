#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."
script="${TMPDIR:-/tmp}/magma-brewing-live-$$.jsonl"
state="${TMPDIR:-/tmp}/magma-brewing-live-state-$$.jsonl"
trap 'rm -f "$script" "$state"' EXIT

make game game/test_brewing_live
game/test_brewing_live

printf '%s\n' \
	'{"tick":0,"type":"set_block","x":8,"y":4,"z":8,"id":117,"meta":1}' \
	'{"tick":0,"type":"set_brewing_slot","dim":0,"x":8,"y":4,"z":8,"slot":0,"item":373,"count":1,"meta":1,"brew_time":200,"fuel":19}' \
	'{"tick":0,"type":"set_brewing_slot","dim":0,"x":8,"y":4,"z":8,"slot":3,"item":372,"count":2,"meta":0,"brew_time":200,"fuel":19}' \
	'{"tick":0,"type":"set_brewing_slot","dim":0,"x":8,"y":4,"z":8,"slot":4,"item":377,"count":1,"meta":0,"brew_time":200,"fuel":19}' \
	>"$script"

./magma_game --world superflat --brewing on --headless --ticks 1 \
	--script "$script" --state-out "$state" --render off --pace unlimited
uv run --no-project python - "$state" <<'PY'
import json
import sys

row = json.loads(open(sys.argv[1], encoding="utf-8").readline())
stands = [
    value for value in row["containers"]
    if value["type"] == "brewing_stand"
]
assert len(stands) == 1, stands
stand = stands[0]
assert (stand["x"], stand["y"], stand["z"]) == (8, 4, 8)
assert stand["size"] == 5
assert stand["brew_time"] == 199 and stand["fuel"] == 19
assert stand["items"] == [
    {"slot": 0, "id": 373, "count": 1, "meta": 1},
    {"slot": 3, "id": 372, "count": 2, "meta": 0},
    {"slot": 4, "id": 377, "count": 1, "meta": 0},
]
PY

# Reload a living entity's air counter immediately before an underwater tick.
# The raw row is sufficient to feed the resulting save state back later.
printf '%s\n' \
	'{"tick":0,"type":"set_block","x":8,"y":6,"z":10,"id":9,"meta":0}' \
	'{"tick":0,"type":"spawn_mob_fixture","entity":12,"eid":705,"x":8.5,"y":5,"z":10.5,"vx":0,"vy":0,"vz":0,"yaw":0,"health":10,"no_ai":1,"hurt_time":0,"death_time":0,"hurt_resistant_time":0}' \
	'{"tick":0,"type":"set_mob_air","eid":705,"air":-19}' \
	>"$script"
./magma_game --world superflat --headless --ticks 1 --mobs on \
	--script "$script" --state-out "$state" --render off --pace unlimited
uv run --no-project python - "$state" <<'PY'
import json
import sys

row = json.loads(open(sys.argv[1], encoding="utf-8").readline())
mob = next(e for e in row["entities"] if e.get("eid") == 705)
assert mob["air"] == 0, mob
assert mob["health"] == 8 and mob["max_health"] == 10, mob
assert mob["hurt_time"] == 9 and mob["hurt_resistant_time"] == 19, mob
assert mob["absorption"] == 0 and mob["potions"] == [], mob
PY

# Resume an in-flight throwable from exact scalar state and observe the next
# vanilla EntityThrowable motion/drag/gravity transition.
printf '%s\n' \
	'{"tick":0,"type":"spawn_potion_fixture","eid":701,"potion_item":438,"potion_type":24,"x":12.5,"y":100,"z":8.5,"vx":0.25,"vy":0.5,"vz":-0.125,"age":7}' \
	>"$script"
./magma_game --world superflat --headless --ticks 1 --mobs off \
	--script "$script" --state-out "$state" --render off --pace unlimited
uv run --no-project python - "$state" <<'PY'
import json
import math
import sys

row = json.loads(open(sys.argv[1], encoding="utf-8").readline())
values = [e for e in row["entities"] if e["kind"] == "projectile"]
assert len(values) == 1, values
potion = values[0]
assert potion["eid"] == 701 and potion["type"] == 6
assert potion["potion_item"] == 438 and potion["potion_type"] == 24
assert potion["age"] == 8
assert (potion["x"], potion["y"], potion["z"]) == (12.75, 100.5, 8.375)
drag = float.fromhex("0x1.fae1480000000p-1")
assert math.isclose(potion["vx"], 0.25 * drag, rel_tol=0, abs_tol=1e-15)
assert math.isclose(potion["vy"], 0.5 * drag - float.fromhex("0x1.99999a0000000p-5"), rel_tol=0, abs_tol=1e-15)
assert math.isclose(potion["vz"], -0.125 * drag, rel_tol=0, abs_tol=1e-15)
PY

# Resume a cloud immediately before a five-tick scan. The output includes all
# lifecycle scalars needed to feed the resulting state back into the fixture.
printf '%s\n' \
	'{"tick":0,"type":"set_pose","x":8.5,"y":5,"z":8.5,"yaw":0,"pitch":0}' \
	'{"tick":0,"type":"spawn_area_effect_cloud_fixture","eid":703,"potion_type":16,"x":8.5,"y":5,"z":8.5,"age":9,"duration":600,"wait_time":10,"reapplication_delay":20,"radius":3,"radius_on_use":-0.5,"radius_per_tick":-0.005,"next_application":0}' \
	>"$script"
./magma_game --world superflat --headless --ticks 1 --mobs off \
	--script "$script" --state-out "$state" --render off --pace unlimited
uv run --no-project python - "$state" <<'PY'
import json
import math
import sys

row = json.loads(open(sys.argv[1], encoding="utf-8").readline())
values = [e for e in row["entities"] if e["kind"] == "area_effect_cloud"]
assert len(values) == 1, values
cloud = values[0]
assert cloud["eid"] == 703 and cloud["potion_type"] == 16
assert cloud["age"] == 10 and cloud["duration"] == 600
assert cloud["wait_time"] == 10 and cloud["reapplication_delay"] == 20
assert cloud["next_application"] == 30
assert math.isclose(cloud["radius"], 2.495, rel_tol=0, abs_tol=2e-7)
assert math.isclose(cloud["radius_on_use"], -0.5, rel_tol=0, abs_tol=1e-8)
assert math.isclose(cloud["radius_per_tick"], -0.005, rel_tol=0, abs_tol=1e-8)
assert row["potions"] == [{"id": 1, "amp": 1, "dur": 450}], (
    row["potions"], (row["x"], row["y"], row["z"]), cloud
)
PY
