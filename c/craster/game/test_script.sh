#!/usr/bin/env bash
# shellcheck disable=SC2129
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
make game
SCRIPT=/tmp/craster-log-route.jsonl
STATE=/tmp/craster-log-route-state.jsonl
: >"$SCRIPT"
printf '%s\n' '{"tick":0,"type":"set_pose","x":0.5,"y":72,"z":2.5,"yaw":0,"pitch":29.25}' >>"$SCRIPT"
for t in $(seq 0 70); do
	printf '{"tick":%d,"type":"action","attack":1,"hotbar":-1}\n' "$t" >>"$SCRIPT"
done
printf '%s\n' '{"tick":81,"type":"set_pose","x":0.5,"y":72,"z":4.5,"yaw":0,"pitch":0}' >>"$SCRIPT"
printf '%s\n' '{"tick":90,"type":"set_pose","x":0.5,"y":72,"z":2.5,"yaw":0,"pitch":3.4336}' >>"$SCRIPT"
for t in $(seq 90 160); do printf '{"tick":%d,"type":"action","attack":1}\n' "$t" >>"$SCRIPT"; done
printf '%s\n' '{"tick":171,"type":"set_pose","x":0.5,"y":73,"z":4.5,"yaw":0,"pitch":0}' >>"$SCRIPT"
printf '%s\n' '{"tick":180,"type":"set_pose","x":0.5,"y":72,"z":2.5,"yaw":0,"pitch":-23.7495}' >>"$SCRIPT"
for t in $(seq 180 250); do printf '{"tick":%d,"type":"action","attack":1}\n' "$t" >>"$SCRIPT"; done
printf '%s\n' '{"tick":261,"type":"set_pose","x":0.5,"y":74,"z":4.5,"yaw":0,"pitch":0}' >>"$SCRIPT"
for t in 270 271 272; do printf '{"tick":%d,"type":"craft","width":2,"grid0":0}\n' "$t" >>"$SCRIPT"; done
printf '%s\n' '{"tick":273,"type":"craft","width":2,"grid0":1,"grid1":1,"grid3":1,"grid4":1}' >>"$SCRIPT"
printf '%s\n' '{"tick":274,"type":"set_pose","x":0.5,"y":72,"z":2.5,"yaw":0,"pitch":60}' >>"$SCRIPT"
printf '%s\n' '{"tick":274,"type":"action","do_place":1,"use":1,"hotbar":0}' >>"$SCRIPT"
printf '%s\n' '{"tick":275,"type":"use_block","x":0,"y":72,"z":3}' >>"$SCRIPT"
printf '%s\n' '{"tick":276,"type":"craft","width":2,"grid0":1,"grid3":1}' >>"$SCRIPT"
printf '%s\n' '{"tick":277,"type":"craft","width":3,"grid0":1,"grid1":1,"grid2":1,"grid4":0,"grid7":0}' >>"$SCRIPT"

# Mine the exposed seed-0 stone seam with the naturally crafted wooden pick.
# Travel hooks move the player, but all blocks, drops, pickup, recipes, tool
# damage, furnace placement, and furnace ticks use authoritative transitions.
printf '%s\n' '{"tick":290,"type":"set_pose","x":-31.5,"y":64,"z":-26.5,"yaw":180,"pitch":29.2488}' >>"$SCRIPT"
for t in $(seq 290 430); do printf '{"tick":%d,"type":"action","attack":1,"hotbar":2}\n' "$t" >>"$SCRIPT"; done
printf '%s\n' \
	'{"tick":440,"type":"set_pose","x":-31.5,"y":64,"z":-28.5,"yaw":180,"pitch":0}' \
	'{"tick":450,"type":"set_pose","x":-31.5,"y":64,"z":-29.5,"yaw":180,"pitch":0}' \
	'{"tick":460,"type":"set_pose","x":-31.5,"y":64,"z":-30.5,"yaw":180,"pitch":0}' \
	'{"tick":480,"type":"set_pose","x":-31.5,"y":63,"z":-30.5,"yaw":180,"pitch":0}' >>"$SCRIPT"
for t in $(seq 480 650); do printf '{"tick":%d,"type":"action","attack":1,"hotbar":2}\n' "$t" >>"$SCRIPT"; done
printf '%s\n' \
	'{"tick":660,"type":"set_pose","x":-31.5,"y":63,"z":-33.5,"yaw":180,"pitch":0}' \
	'{"tick":670,"type":"set_pose","x":-31.5,"y":63,"z":-34.5,"yaw":180,"pitch":0}' \
	'{"tick":690,"type":"set_pose","x":-31.5,"y":63,"z":-34.5,"yaw":180,"pitch":0}' >>"$SCRIPT"
for t in $(seq 690 900); do printf '{"tick":%d,"type":"action","attack":1,"hotbar":2}\n' "$t" >>"$SCRIPT"; done
printf '%s\n' \
	'{"tick":910,"type":"set_pose","x":-31.5,"y":61,"z":-37.5,"yaw":180,"pitch":0}' \
	'{"tick":920,"type":"set_pose","x":-31.5,"y":61,"z":-38.5,"yaw":180,"pitch":0}' \
	'{"tick":930,"type":"set_pose","x":-31.5,"y":61,"z":-39.5,"yaw":180,"pitch":0}' \
	'{"tick":950,"type":"set_pose","x":-31.5,"y":60,"z":-39.5,"yaw":180,"pitch":0}' >>"$SCRIPT"
for t in $(seq 950 1100); do printf '{"tick":%d,"type":"action","attack":1,"hotbar":2}\n' "$t" >>"$SCRIPT"; done
printf '%s\n' \
	'{"tick":1110,"type":"set_pose","x":-31.5,"y":61,"z":-41.5,"yaw":180,"pitch":0}' \
	'{"tick":1120,"type":"set_pose","x":-31.5,"y":61,"z":-42.5,"yaw":180,"pitch":0}' \
	'{"tick":1130,"type":"set_pose","x":-31.5,"y":61,"z":-43.5,"yaw":180,"pitch":0}' \
	'{"tick":1140,"type":"set_pose","x":-31.5,"y":61,"z":-44.5,"yaw":180,"pitch":0}' \
	'{"tick":1160,"type":"set_pose","x":0.5,"y":72,"z":2.5,"yaw":0,"pitch":0}' \
	'{"tick":1161,"type":"use_block","x":0,"y":72,"z":3}' \
	'{"tick":1162,"type":"craft","width":3,"grid0":3,"grid1":3,"grid2":3,"grid3":3,"grid5":3,"grid6":3,"grid7":3,"grid8":3}' \
	'{"tick":1163,"type":"craft","width":3,"grid0":3,"grid1":3,"grid2":3,"grid4":0,"grid7":0}' \
	'{"tick":1180,"type":"set_pose","x":-29.5,"y":44,"z":-13.5,"yaw":90,"pitch":-55.222}' >>"$SCRIPT"
for t in $(seq 1180 1350); do printf '{"tick":%d,"type":"action","attack":1,"hotbar":0}\n' "$t" >>"$SCRIPT"; done
printf '%s\n' \
	'{"tick":1380,"type":"set_pose","x":-31.5,"y":45,"z":-13.5,"yaw":90,"pitch":0}' \
	'{"tick":1400,"type":"set_pose","x":1.5,"y":72,"z":2.5,"yaw":0,"pitch":60}' \
	'{"tick":1400,"type":"action","do_place":1,"use":1,"hotbar":5}' \
	'{"tick":1401,"type":"use_block","x":1,"y":72,"z":3}' \
	'{"tick":1402,"type":"furnace_insert","slot":0,"inventory":6,"count":1}' \
	'{"tick":1403,"type":"furnace_insert","slot":1,"inventory":1,"count":1}' \
	'{"tick":1605,"type":"furnace_extract","slot":2,"count":1}' >>"$SCRIPT"

./craster_game --seed 0 --world default --view-distance 1 --headless --ticks 1610 \
	--script "$SCRIPT" --state-out "$STATE" --render off --pace unlimited
./craster_game --seed 0 --world default --view-distance 1 --headless --ticks 1610 \
	--script "$SCRIPT" --state-out "${STATE}.repeat" --render off --pace unlimited
cmp "$STATE" "${STATE}.repeat"
uv run --no-project python - "$STATE" <<'PY'
import json
import sys
rows = [json.loads(line) for line in open(sys.argv[1], encoding="utf-8")]
assert len(rows) == 1610
assert rows[0]["version"] == 1 and rows[-1]["tick"] == 1610
assert any(e.get("item") == 17 for row in rows for e in row["entities"])
assert any(s["item"] == 17 and s["count"] == 3 for row in rows for s in row["inventory"])
assert any(e.get("item") == 4 for row in rows for e in row["entities"])
assert any(e.get("item") == 15 for row in rows for e in row["entities"])
assert any(s["item"] == 270 and s["meta"] >= 17 for s in rows[-1]["inventory"])
assert any(s["item"] == 274 and s["meta"] == 3 for s in rows[-1]["inventory"])
assert any(s["item"] == 265 and s["count"] == 1 for s in rows[-1]["inventory"])
assert any(row["furnace"] and row["furnace"]["burn"] > 0 for row in rows)
assert rows[-1]["container"] == 2
assert rows[-1]["dead"] == 0 and rows[-1]["won"] is False
PY
printf '%s\n' '{"tick":0,"type":"won"}' >/tmp/craster-forbidden.jsonl
if ./craster_game --headless --ticks 1 --script /tmp/craster-forbidden.jsonl \
	--render off --pace unlimited >/tmp/craster-forbidden.out 2>&1; then
	echo "forbidden progression injection unexpectedly succeeded" >&2
	exit 1
fi
rg -q 'unknown or forbidden type: won' /tmp/craster-forbidden.out
printf '%s\n' '{"tick":0,"type":"set_pose","x":0,"y":70,"z":0,"yaw":0,"pitch":0,"inventory":17}' \
	>/tmp/craster-forbidden-field.jsonl
if ./craster_game --headless --ticks 1 --script /tmp/craster-forbidden-field.jsonl \
	--render off --pace unlimited >/tmp/craster-forbidden-field.out 2>&1; then
	echo "forbidden state field unexpectedly succeeded" >&2
	exit 1
fi
rg -q 'unknown or forbidden field: inventory' /tmp/craster-forbidden-field.out

# Post-2026-07-12 tape schema: exact entity render state, post-tick inventory
# view, offhand re-anchor, and HUD XP/air all parse as strict typed events.
TAPE_STATE=/tmp/craster-tape-state.jsonl
printf '%s\n' \
	'{"tick":0,"type":"snapshot_region","cx":0,"cz":0,"radius":0}' \
	'{"tick":0,"type":"snapshot_block","x":0,"y":200,"z":0,"id":1,"meta":0}' \
	'{"tick":0,"type":"snapshot_region","dim":-1,"cx":0,"cz":0,"radius":0}' \
	'{"tick":0,"type":"snapshot_block","dim":-1,"x":0,"y":200,"z":0,"id":87,"meta":0}' \
	'{"tick":0,"type":"inv_view","slot":0,"item":17,"count":2,"meta":0}' \
	'{"tick":0,"type":"inv_view","slot":40,"item":442,"count":1,"meta":0}' \
	'{"tick":0,"type":"player_view","xp_level":7,"xp_frac":0.625,"air":123}' \
	'{"tick":0,"type":"ent_view","ent":"EntitySheep","x":1,"y":64,"z":2,"yaw":30,"hp":8,"id":7,"tape_pose":1,"head_yaw":55,"pitch":12,"swing":0.25,"hurt":4,"death":2,"body_yaw":28,"flags":3,"sheared":1,"fleece":14,"graze_y":0.75,"graze_x":1.1}' \
	'{"tick":0,"type":"ent_view","ent":"EntityItem","x":2,"y":64,"z":3,"yaw":0,"hp":-1,"id":8,"item":318,"item_meta":0,"count":3,"age":12,"hover":1.25,"has_hover":1}' \
	'{"tick":1,"type":"set_inventory","slot":40,"item":442,"count":1,"meta":0}' \
	>"$TAPE_STATE"
./craster_game --headless --ticks 2 --script "$TAPE_STATE" --render off --pace unlimited \
	>/tmp/craster-tape-state.out
rg -q '"tick":2' /tmp/craster-tape-state.out
printf '%s\n' '{"tick":1,"type":"snapshot_block","x":0,"y":200,"z":0,"id":1,"meta":0}' \
	>/tmp/craster-late-snapshot.jsonl
# Cross-dimension position packets may arrive after tick zero. Their bounded
# snapshot neighborhood must be reloadable at that authoritative arrival tick.
./craster_game --headless --ticks 2 --script /tmp/craster-late-snapshot.jsonl \
	--render off --pace unlimited >/tmp/craster-late-snapshot.out
rg -q '"tick":2' /tmp/craster-late-snapshot.out

# Tape dimension/vitals events are strict test-only transitions. Dimension is
# selected before the tick; vitals truth is applied after it for state output.
printf '%s\n' \
	'{"tick":0,"type":"set_dimension","dimension":-1}' \
	'{"tick":0,"type":"set_vitals_post","health":17,"food":19}' \
	>/tmp/craster-tape-dimension.jsonl
./craster_game --headless --ticks 1 --script /tmp/craster-tape-dimension.jsonl \
	--state-out /tmp/craster-tape-dimension-state.jsonl --render off --pace unlimited
rg -q '"dim":-1' /tmp/craster-tape-dimension-state.jsonl
rg -q '"health":17' /tmp/craster-tape-dimension-state.jsonl
rg -q '"food":19' /tmp/craster-tape-dimension-state.jsonl

# Loading-terrain pose truth is applied after physics, including velocity,
# on-ground, and fall distance, so the state row matches the frozen client.
printf '%s\n' \
	'{"tick":0,"type":"set_pose","x":8.5,"y":70,"z":8.5,"yaw":0,"pitch":0}' \
	'{"tick":0,"type":"set_velocity","x":0,"y":-1,"z":0}' \
	'{"tick":0,"type":"set_pose_post","x":24.5,"y":76,"z":24.5,"yaw":270,"pitch":5,"vx":0.25,"vy":0,"vz":-0.5,"on_ground":0,"fall":1.25}' \
	>/tmp/craster-pose-post.jsonl
./craster_game --headless --ticks 1 --script /tmp/craster-pose-post.jsonl \
	--state-out /tmp/craster-pose-post-state.jsonl --render off --pace unlimited
uv run --no-project python - <<'PY'
import json
row = json.loads(open("/tmp/craster-pose-post-state.jsonl", encoding="utf-8").read())
assert (row["x"], row["y"], row["z"]) == (24.5, 76, 24.5), row
assert (row["yaw"], row["pitch"]) == (270, 5), row
assert (row["vx"], row["vy"], row["vz"]) == (0.25, 0, -0.5), row
assert row["on_ground"] == 0, row
PY

# gui_view (OPEN_DIVERGENCES #9): mapped class drives a container panel over the
# finished frame; unmapped classes are logged once and skipped (physics clean).
printf '%s\n' \
	'{"tick":0,"type":"set_pose","x":8.5,"y":72,"z":8.5,"yaw":0,"pitch":0}' \
	'{"tick":0,"type":"set_inventory","slot":0,"item":5,"count":4,"meta":0}' \
	'{"tick":0,"type":"gui_view","gui":"GuiCrafting","mx":213,"my":120}' \
	'{"tick":0,"type":"gui_slot_view","slot":36,"item":5,"count":3,"meta":2}' \
	'{"tick":0,"type":"gui_cursor_view","item":17,"count":2,"meta":1}' \
	'{"tick":0,"type":"gui_furnace_view","burn":80,"current_burn":1600,"cook":100,"total_cook":200}' \
	'{"tick":0,"type":"gui_view","gui":"GuiIngameMenu","mx":0,"my":0}' \
	>/tmp/craster-gui-view.jsonl
rm -rf /tmp/craster-gui-view-frames
mkdir -p /tmp/craster-gui-view-frames
./craster_game --world superflat --headless --ticks 1 --script /tmp/craster-gui-view.jsonl \
	--state-out /tmp/craster-gui-view-state.jsonl \
	--frames-out /tmp/craster-gui-view-frames --width 854 --height 480 \
	--pace unlimited >/tmp/craster-gui-view.out 2>/tmp/craster-gui-view.err
rg -q 'gui_view GuiIngameMenu: no container screen, skipped' /tmp/craster-gui-view.err
# physics unchanged by render-only gui_view (container stays closed)
uv run --no-project python - <<'PY'
import json, os, struct
row = json.loads(open("/tmp/craster-gui-view-state.jsonl", encoding="utf-8").read())
assert row["container"] == 0, row["container"]
assert row["dead"] == 0
# at least one PPM written for the gui frame
ppms = [f for f in os.listdir("/tmp/craster-gui-view-frames") if f.endswith(".ppm")]
assert ppms, "expected a frames-out ppm"
# PPM is dimmed + panel-colored (not pure sky): mean luma below fullbright
path = os.path.join("/tmp/craster-gui-view-frames", sorted(ppms)[0])
data = open(path, "rb").read()
# skip P6 header
i = 0
assert data.startswith(b"P6")
while data[i:i+1] != b"\n": i += 1
i += 1
if data[i:i+1] == b"#":
    while data[i:i+1] != b"\n": i += 1
    i += 1
dims = data[i:].split(b"\n", 1)[0]
w, h = map(int, dims.split())
i += len(dims) + 1
while data[i:i+1] != b"\n": i += 1
i += 1
rgb = data[i:]
assert w == 854 and h == 480 and len(rgb) >= w * h * 3
# sample panel center (vanilla origin ~250,157 at 854x480 scale 2, panel 352x332)
cx, cy = 250 + 176, 157 + 166  # roughly panel center in fb px
off = (cy * w + cx) * 3
r, g, b = rgb[off], rgb[off+1], rgb[off+2]
# background dim is dark gray; panel blit is brighter than pure dim (16,16,16)
assert not (r == 16 and g == 16 and b == 16), (r, g, b)
print("gui_view craft panel smoke: ok", path, "panel_px", (r, g, b))
PY

printf '%s\n' \
	'{"tick":0,"type":"set_pose","x":8.5,"y":10,"z":8.5,"yaw":0,"pitch":0}' \
	'{"tick":0,"type":"set_velocity","x":1,"y":0,"z":0}' \
	'{"tick":0,"type":"set_time","value":13000}' >/tmp/craster-travel-hooks.jsonl
./craster_game --world superflat --headless --ticks 1 --script /tmp/craster-travel-hooks.jsonl \
	--state-out /tmp/craster-travel-hooks-state.jsonl --render off --pace unlimited
uv run --no-project python - <<'PY'
import json
row = json.loads(open("/tmp/craster-travel-hooks-state.jsonl", encoding="utf-8").read())
assert row["world_time"] == 13001
assert row["vx"] > 0.0
PY
printf '%s\n' \
	'{"tick":0,"type":"set_pose","x":8.5,"y":10,"z":8.5,"yaw":0,"pitch":90}' \
	'{"tick":0,"type":"set_block","x":8,"y":7,"z":8,"id":57,"meta":0}' \
	'{"tick":0,"type":"set_inventory","slot":0,"item":276,"count":1,"meta":12}' \
	'{"tick":0,"type":"set_weather","raining":1,"thundering":1,"rain_time":100,"thunder_time":200}' \
	>/tmp/craster-state-hooks.jsonl
./craster_game --world superflat --headless --ticks 1 --script /tmp/craster-state-hooks.jsonl \
	--state-out /tmp/craster-state-hooks-state.jsonl --render off --pace unlimited
uv run --no-project python - <<'PY'
import json
row = json.loads(open("/tmp/craster-state-hooks-state.jsonl", encoding="utf-8").read())
assert row["look"]["id"] == 57
assert row["inventory"][0] == {"slot": 0, "item": 276, "count": 1, "meta": 12}
assert row["weather"] == {
    "raining": 1,
    "thundering": 1,
    "rain_time": 99,
    "thunder_time": 199,
}
PY
# Container.slotClick as a survival action: seed a log with the test hook, click
# it onto the cursor, place ONE in the 2x2 grid, and shift-craft the result. The
# clicks themselves are ordinary actions through the shared runtime tick.
printf '%s\n' \
	'{"tick":0,"type":"set_inventory","slot":0,"item":17,"count":2,"meta":0}' \
	'{"tick":1,"type":"action","inv_slot":0,"inv_button":0,"inv_type":0}' \
	'{"tick":2,"type":"action","inv_slot":36,"inv_button":1,"inv_type":0}' \
	'{"tick":3,"type":"action","inv_slot":0,"inv_button":0,"inv_type":0}' \
	'{"tick":4,"type":"action","inv_slot":45,"inv_button":0,"inv_type":1}' \
	>/tmp/craster-inv-click.jsonl
./craster_game --world superflat --headless --ticks 5 --script /tmp/craster-inv-click.jsonl \
	--state-out /tmp/craster-inv-click-state.jsonl --render off --pace unlimited
uv run --no-project python - <<'PY'
import json
rows = [json.loads(l) for l in open("/tmp/craster-inv-click-state.jsonl", encoding="utf-8")]
after_grid = rows[2]
assert after_grid["grid"][0] == [17, 1, 0], after_grid["grid"]
assert after_grid["craft_result"] == [5, 4, 0], after_grid["craft_result"]
final = rows[-1]
assert final["grid"][0] == [0, 0, 0]
assert any(s["item"] == 5 and s["count"] == 4 for s in final["inventory"])
assert any(s["item"] == 17 and s["count"] == 1 for s in final["inventory"])
PY
printf '%s\n' \
	'{"tick":0,"type":"action","inv_slot":49,"inv_button":0,"inv_type":0}' \
	>/tmp/craster-invalid-click.jsonl
if ./craster_game --world superflat --headless --ticks 1 --script /tmp/craster-invalid-click.jsonl \
	--render off --pace unlimited >/tmp/craster-invalid-click.out 2>&1; then
	echo "out-of-range inv_slot unexpectedly succeeded" >&2
	exit 1
fi
rg -q 'invalid inv_slot' /tmp/craster-invalid-click.out

printf '%s\n' \
	'{"tick":0,"type":"set_inventory","slot":0,"item":0,"count":1,"meta":0}' \
	>/tmp/craster-invalid-state-hook.jsonl
if ./craster_game --headless --ticks 1 --script /tmp/craster-invalid-state-hook.jsonl \
	--render off --pace unlimited >/tmp/craster-invalid-state-hook.out 2>&1; then
	echo "invalid inventory injection unexpectedly succeeded" >&2
	exit 1
fi
rg -q 'invalid set_inventory' /tmp/craster-invalid-state-hook.out
printf '%s\n' \
	'{"tick":0,"type":"set_pose","x":8.5,"y":5,"z":8.5,"yaw":0,"pitch":0}' \
	'{"tick":0,"type":"spawn_entity","entity":2,"x":8.5,"y":5,"z":14.5}' \
	>/tmp/craster-entity-hook.jsonl
./craster_game --world superflat --headless --ticks 1 --script /tmp/craster-entity-hook.jsonl \
	--state-out /tmp/craster-entity-hook-state.jsonl --render off --pace unlimited
uv run --no-project python - <<'PY'
import json
row = json.loads(open("/tmp/craster-entity-hook-state.jsonl", encoding="utf-8").read())
assert any(entity["type"] == 2 and entity["health"] == 20 for entity in row["entities"])
PY
rm -rf /tmp/craster-script-frames-a /tmp/craster-script-frames-b
printf '%s\n' \
	'{"tick":0,"type":"set_time","value":0}' \
	'{"tick":1,"type":"set_time","value":12000}' \
	>/tmp/craster-frame-time.jsonl
for dir in /tmp/craster-script-frames-a /tmp/craster-script-frames-b; do
	./craster_game --world superflat --view-distance 1 --width 160 --height 90 \
		--headless --ticks 2 --script /tmp/craster-frame-time.jsonl \
		--render off --pace unlimited --frames-out "$dir" \
		--state-out "$dir.jsonl"
done
cmp /tmp/craster-script-frames-a/frame_000000.ppm /tmp/craster-script-frames-b/frame_000000.ppm
cmp /tmp/craster-script-frames-a/frame_000001.ppm /tmp/craster-script-frames-b/frame_000001.ppm
cmp /tmp/craster-script-frames-a.jsonl /tmp/craster-script-frames-b.jsonl
if cmp -s /tmp/craster-script-frames-a/frame_000000.ppm \
	/tmp/craster-script-frames-a/frame_000001.ppm; then
	echo "day and night frame captures unexpectedly match" >&2
	exit 1
fi
uv run --no-project python - <<'PY'
import json
from pathlib import Path

frames = sorted(Path("/tmp/craster-script-frames-a").glob("frame_*.ppm"))
assert [frame.name for frame in frames] == ["frame_000000.ppm", "frame_000001.ppm"]
assert all(frame.read_bytes().startswith(b"P6\n160 90\n255\n") for frame in frames)
states = [json.loads(line) for line in Path("/tmp/craster-script-frames-a.jsonl").read_text().splitlines()]
assert [state["world_time"] for state in states] == [1, 12001]
PY
echo "script route: PASS"
