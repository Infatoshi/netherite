#!/usr/bin/env bash
# shellcheck disable=SC2129
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
make game
BLOCK_RNG_SCRIPT=/tmp/magma-block-rng.jsonl
BLOCK_RNG_STATE=/tmp/magma-block-rng-state.jsonl
printf '%s\n' \
	'{"tick":0,"type":"set_block_random_seed","value":188900966474565}' \
	>"$BLOCK_RNG_SCRIPT"
./magma_game --headless --ticks 1 --script "$BLOCK_RNG_SCRIPT" \
	--state-out "$BLOCK_RNG_STATE" --render off --pace unlimited
uv run --no-project python - "$BLOCK_RNG_STATE" <<'PY'
import json
import sys
row = json.loads(open(sys.argv[1], encoding="utf-8").readline())
assert row["block_rand_seed48"] == 188900966474565
PY
CONTROLLED_SCRIPT=/tmp/magma-controlled-input.jsonl
CONTROLLED_STATE=/tmp/magma-controlled-input-state.jsonl
printf '%s\n' \
	'{"tick":0,"type":"set_entity_id_cursor","value":123}' \
	'{"tick":0,"type":"set_world_random_seed","value":1}' \
	'{"tick":0,"type":"set_math_random_seed","value":2}' \
	'{"tick":0,"type":"set_block_random_seed","value":3}' \
	'{"tick":0,"type":"set_world_update_lcg","value":-4}' \
	'{"tick":0,"type":"begin_controlled_input"}' \
	'{"tick":0,"type":"set_world_random_seed","value":5}' \
	'{"tick":0,"type":"capture_controlled_input"}' \
	>"$CONTROLLED_SCRIPT"
./magma_game --headless --ticks 1 --script "$CONTROLLED_SCRIPT" \
	--state-out "$CONTROLLED_STATE" --render off --pace unlimited
uv run --no-project python - "$CONTROLLED_STATE" <<'PY'
import json
import sys
row = json.loads(open(sys.argv[1], encoding="utf-8").readline())
assert row["world_rand_seed48"] == 5
assert row["controlled_input"] == {
    "before": {
        "world_rand_seed48": 1,
        "math_rand_seed48": 2,
        "block_rand_seed48": 3,
        "world_update_lcg": -4,
        "next_entity_id": 123,
    },
    "world_rand_seed48": 5,
    "math_rand_seed48": 2,
    "block_rand_seed48": 3,
    "world_update_lcg": -4,
    "next_entity_id": 123,
}
PY
uv run --no-project python - <<'PY'
import importlib.util

spec = importlib.util.spec_from_file_location(
    "magma_diff_trace", "trace/diff_trace.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

def advanced(seed, steps):
    for _ in range(steps):
        seed = (seed * module.JAVA_RANDOM_MULTIPLIER
                + module.JAVA_RANDOM_ADDEND) & module.JAVA_RANDOM_MASK
    return seed

def bundle(seed, steps):
    return {
        "before": {
            "entity_id_cursor": 10,
            "world_rng": {
                "java_seed48": seed,
                "math_seed48": seed + 1,
                "block_seed48": seed + 2,
                "update_lcg": -4,
            },
        },
        "entity_id_cursor": 11,
        "world_rng": {
            "java_seed48": advanced(seed, steps),
            "math_seed48": advanced(seed + 1, 2),
            "block_seed48": seed + 2,
            "update_lcg": 7,
        },
    }

java = [{"tick": 0, "controlled_input": bundle(1, 3)}]
c_same_transition = [{"tick": 0, "controlled_input": bundle(101, 3)}]
c_wrong_transition = [{"tick": 0, "controlled_input": bundle(101, 4)}]
assert module.diff_controlled_input(java, c_same_transition, 1).status == "match"
assert module.diff_controlled_input(java, c_wrong_transition, 1).status == "diverge"
PY
SCRIPT=/tmp/magma-log-route.jsonl
STATE=/tmp/magma-log-route-state.jsonl
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
	'{"tick":1180,"type":"set_block","x":-30,"y":44,"z":-14,"id":0,"meta":0}' \
	'{"tick":1180,"type":"set_block","x":-30,"y":45,"z":-14,"id":0,"meta":0}' \
	'{"tick":1180,"type":"set_block","x":-32,"y":45,"z":-14,"id":0,"meta":0}' \
	'{"tick":1180,"type":"set_block","x":-32,"y":46,"z":-14,"id":0,"meta":0}' \
	'{"tick":1180,"type":"set_pose","x":-29.5,"y":44,"z":-13.5,"yaw":90,"pitch":-55.222}' >>"$SCRIPT"
# This long route test targets mining/crafting/furnace progression. The natural
# seam contains lava that can flow into the held mining pose; pin fire/vitals
# explicitly so the separate fire-contact regression owns that behavior.
for t in $(seq 1180 1379); do
	if [ "$t" -le 1350 ]; then
		printf '{"tick":%d,"type":"action","attack":1,"hotbar":0}\n' "$t" >>"$SCRIPT"
	fi
	printf '{"tick":%d,"type":"set_fire","fire":-20}\n' "$t" >>"$SCRIPT"
	printf '{"tick":%d,"type":"set_vitals_post","health":20,"food":20}\n' "$t" >>"$SCRIPT"
done
printf '%s\n' \
	'{"tick":1380,"type":"set_pose","x":-31.5,"y":45,"z":-13.5,"yaw":90,"pitch":0}' \
	'{"tick":1380,"type":"set_fire","fire":-20}' \
	'{"tick":1380,"type":"set_vitals_post","health":20,"food":20}' \
	'{"tick":1400,"type":"set_pose","x":1.5,"y":72,"z":2.5,"yaw":0,"pitch":60}' \
	'{"tick":1400,"type":"action","do_place":1,"use":1,"hotbar":4}' \
	'{"tick":1401,"type":"use_block","x":1,"y":72,"z":3}' \
	'{"tick":1402,"type":"furnace_insert","slot":0,"inventory":5,"count":1}' \
	'{"tick":1403,"type":"furnace_insert","slot":1,"inventory":1,"count":1}' \
	'{"tick":1605,"type":"furnace_extract","slot":2,"count":1}' >>"$SCRIPT"

./magma_game --seed 0 --world default --view-distance 1 --headless --ticks 1610 \
	--script "$SCRIPT" --state-out "$STATE" --render off --pace unlimited
./magma_game --seed 0 --world default --view-distance 1 --headless --ticks 1610 \
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
printf '%s\n' '{"tick":0,"type":"won"}' >/tmp/magma-forbidden.jsonl
if ./magma_game --headless --ticks 1 --script /tmp/magma-forbidden.jsonl \
	--render off --pace unlimited >/tmp/magma-forbidden.out 2>&1; then
	echo "forbidden progression injection unexpectedly succeeded" >&2
	exit 1
fi
rg -q 'unknown or forbidden type: won' /tmp/magma-forbidden.out
printf '%s\n' '{"tick":0,"type":"set_pose","x":0,"y":70,"z":0,"yaw":0,"pitch":0,"inventory":17}' \
	>/tmp/magma-forbidden-field.jsonl
if ./magma_game --headless --ticks 1 --script /tmp/magma-forbidden-field.jsonl \
	--render off --pace unlimited >/tmp/magma-forbidden-field.out 2>&1; then
	echo "forbidden state field unexpectedly succeeded" >&2
	exit 1
fi
rg -q 'unknown or forbidden field: inventory' /tmp/magma-forbidden-field.out

# Post-2026-07-12 tape schema: exact entity render state, post-tick inventory
# view, offhand re-anchor, and HUD XP/air all parse as strict typed events.
TAPE_STATE=/tmp/magma-tape-state.jsonl
printf '%s\n' \
	'{"tick":0,"type":"snapshot_region","cx":0,"cz":0,"radius":0}' \
	'{"tick":0,"type":"snapshot_block","x":0,"y":200,"z":0,"id":1,"meta":0}' \
	'{"tick":0,"type":"snapshot_region","dim":-1,"cx":0,"cz":0,"radius":0}' \
	'{"tick":0,"type":"snapshot_block","dim":-1,"x":0,"y":200,"z":0,"id":87,"meta":0}' \
	'{"tick":0,"type":"inv_view","slot":0,"item":17,"count":2,"meta":0}' \
	'{"tick":0,"type":"inv_view","slot":40,"item":442,"count":1,"meta":0}' \
	'{"tick":0,"type":"player_view","xp_level":7,"xp_frac":0.625,"air":123,"fire":1,"creative":0,"hurt":9}' \
	'{"tick":0,"type":"ent_view","ent":"EntitySheep","x":1,"y":64,"z":2,"yaw":30,"hp":8,"id":7,"tape_pose":1,"head_yaw":55,"pitch":12,"swing":0.25,"hurt":4,"death":2,"body_yaw":28,"flags":3,"sheared":1,"fleece":14,"graze_y":0.75,"graze_x":1.1}' \
	'{"tick":0,"type":"ent_view","ent":"EntityItem","x":2,"y":64,"z":3,"yaw":0,"hp":-1,"id":8,"item":318,"item_meta":0,"count":3,"age":12,"hover":1.25,"has_hover":1}' \
	'{"tick":1,"type":"set_inventory","slot":40,"item":442,"count":1,"meta":0}' \
	>"$TAPE_STATE"
./magma_game --headless --ticks 2 --script "$TAPE_STATE" --render off --pace unlimited \
	>/tmp/magma-tape-state.out
rg -q '"tick":2' /tmp/magma-tape-state.out
printf '%s\n' '{"tick":1,"type":"snapshot_block","x":0,"y":200,"z":0,"id":1,"meta":0}' \
	>/tmp/magma-late-snapshot.jsonl
# Cross-dimension position packets may arrive after tick zero. Their bounded
# snapshot neighborhood must be reloadable at that authoritative arrival tick.
./magma_game --headless --ticks 2 --script /tmp/magma-late-snapshot.jsonl \
	--render off --pace unlimited >/tmp/magma-late-snapshot.out
rg -q '"tick":2' /tmp/magma-late-snapshot.out

# Tape dimension/vitals events are strict test-only transitions. Dimension is
# selected before the tick; vitals truth is applied after it for state output.
printf '%s\n' \
	'{"tick":0,"type":"set_dimension","dimension":-1}' \
	'{"tick":0,"type":"set_vitals_post","health":17,"food":19}' \
	>/tmp/magma-tape-dimension.jsonl
./magma_game --headless --ticks 1 --script /tmp/magma-tape-dimension.jsonl \
	--state-out /tmp/magma-tape-dimension-state.jsonl --render off --pace unlimited
rg -q '"dim":-1' /tmp/magma-tape-dimension-state.jsonl
rg -q '"health":17' /tmp/magma-tape-dimension-state.jsonl
rg -q '"food":19' /tmp/magma-tape-dimension-state.jsonl

# FoodStats.onUpdate can run before the integrated-server health packet is
# visible to a client-tick tape. Hold the early visible heal, then expose it
# without applying its exhaustion/timer side effects a second time.
: >/tmp/magma-regen-hold.jsonl
printf '%s\n' \
	'{"tick":0,"type":"set_vitals","health":14,"food":20}' \
	'{"tick":0,"type":"set_food_stats_post","saturation":3,"exhaustion":0}' \
	>>/tmp/magma-regen-hold.jsonl
for t in $(seq 1 9); do
	printf '{"tick":%d,"type":"hold_regen_post"}\n' "$t" \
		>>/tmp/magma-regen-hold.jsonl
done
printf '%s\n' \
	'{"tick":10,"type":"set_regen_post","health":14.5,"food":20,"exhaustion":3}' \
	>>/tmp/magma-regen-hold.jsonl
./magma_game --headless --ticks 20 --script /tmp/magma-regen-hold.jsonl \
	--state-out /tmp/magma-regen-hold-state.jsonl --render off --pace unlimited
uv run --no-project python - <<'PY'
import json
rows = [json.loads(line) for line in open(
    "/tmp/magma-regen-hold-state.jsonl", encoding="utf-8")]
assert rows[9]["health"] == 14.0, rows[9]
assert rows[10]["health"] == 14.5, rows[10]
assert rows[19]["health"] == 15.0, rows[19]
PY

# Loading-terrain pose truth is applied after physics, including velocity,
# on-ground, and fall distance, so the state row matches the frozen client.
printf '%s\n' \
	'{"tick":0,"type":"set_pose","x":8.5,"y":70,"z":8.5,"yaw":0,"pitch":0}' \
	'{"tick":0,"type":"set_velocity","x":0,"y":-1,"z":0}' \
	'{"tick":0,"type":"set_pose_post","x":24.5,"y":76,"z":24.5,"yaw":270,"pitch":5,"vx":0.25,"vy":0,"vz":-0.5,"on_ground":0,"fall":1.25}' \
	>/tmp/magma-pose-post.jsonl
./magma_game --headless --ticks 1 --script /tmp/magma-pose-post.jsonl \
	--state-out /tmp/magma-pose-post-state.jsonl --render off --pace unlimited
uv run --no-project python - <<'PY'
import json
row = json.loads(open("/tmp/magma-pose-post-state.jsonl", encoding="utf-8").read())
assert (row["x"], row["y"], row["z"]) == (24.5, 76, 24.5), row
assert (row["yaw"], row["pitch"]) == (270, 5), row
assert (row["vx"], row["vy"], row["vz"]) == (0.25, 0, -0.5), row
assert row["on_ground"] == 0, row
PY

# The A/B oracle must be able to inject the complete uncontaminated pre-tick
# travel state. A plain set_pose zeroes velocity/onGround and would fabricate a
# tick-0 divergence after the Java teleport has already settled.
printf '%s\n' \
	'{"tick":0,"type":"set_pose_state","x":8.5,"y":70,"z":8.5,"yaw":-180,"pitch":5,"vx":0.25,"vy":-0.0784000015258789,"vz":-0.5,"on_ground":1,"fall":1.25}' \
	>/tmp/magma-pose-state.jsonl
./magma_game --world superflat --headless --ticks 1 \
	--script /tmp/magma-pose-state.jsonl \
	--state-out /tmp/magma-pose-state-state.jsonl --render off --pace unlimited
uv run --no-project python - <<'PY'
import json
row = json.loads(open("/tmp/magma-pose-state-state.jsonl", encoding="utf-8").read())
assert row["x"] != 8.5 or row["z"] != 8.5, row  # velocity participated in tick 0
assert row["fall_distance"] >= 0.0, row
assert row["saturation"] == 5.0, row
assert row["air"] == 300 and row["attack_cooldown"] == 1, row
assert row["fire"] == -20 and row["potions"] == [], row
PY

# A capsule-loaded positive Entity.fire counter ages before movement. Damage at
# a pre-decrement multiple of 20 creates vanilla's same-tick 10-to-9 hurt timer.
printf '%s\n' \
	'{"tick":0,"type":"set_fire","fire":42}' \
	>/tmp/magma-fire-counter.jsonl
./magma_game --world superflat --headless --ticks 3 \
	--script /tmp/magma-fire-counter.jsonl \
	--state-out /tmp/magma-fire-counter-state.jsonl --render off --pace unlimited
uv run --no-project python - <<'PY'
import json
rows = [
    json.loads(line)
    for line in open("/tmp/magma-fire-counter-state.jsonl", encoding="utf-8")
]
assert [(row["fire"], row["health"], row["hurt_time"]) for row in rows] == [
    (41, 20, 0),
    (40, 20, 0),
    (39, 19, 9),
], rows
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
	>/tmp/magma-gui-view.jsonl
rm -rf /tmp/magma-gui-view-frames
mkdir -p /tmp/magma-gui-view-frames
./magma_game --world superflat --headless --ticks 1 --script /tmp/magma-gui-view.jsonl \
	--state-out /tmp/magma-gui-view-state.jsonl \
	--frames-out /tmp/magma-gui-view-frames --width 854 --height 480 \
	--pace unlimited >/tmp/magma-gui-view.out 2>/tmp/magma-gui-view.err
rg -q 'gui_view GuiIngameMenu: no container screen, skipped' /tmp/magma-gui-view.err
# physics unchanged by render-only gui_view (container stays closed)
uv run --no-project python - <<'PY'
import json, os, struct
row = json.loads(open("/tmp/magma-gui-view-state.jsonl", encoding="utf-8").read())
assert row["container"] == 0, row["container"]
assert row["dead"] == 0
# at least one PPM written for the gui frame
ppms = [f for f in os.listdir("/tmp/magma-gui-view-frames") if f.endswith(".ppm")]
assert ppms, "expected a frames-out ppm"
# PPM is dimmed + panel-colored (not pure sky): mean luma below fullbright
path = os.path.join("/tmp/magma-gui-view-frames", sorted(ppms)[0])
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
	'{"tick":0,"type":"set_time","value":13000}' >/tmp/magma-travel-hooks.jsonl
./magma_game --world superflat --headless --ticks 1 --script /tmp/magma-travel-hooks.jsonl \
	--state-out /tmp/magma-travel-hooks-state.jsonl --render off --pace unlimited
uv run --no-project python - <<'PY'
import json
row = json.loads(open("/tmp/magma-travel-hooks-state.jsonl", encoding="utf-8").read())
assert row["world_time"] == 13001
assert row["vx"] > 0.0
PY
printf '%s\n' \
	'{"tick":0,"type":"set_pose","x":8.5,"y":10,"z":8.5,"yaw":0,"pitch":90}' \
	'{"tick":0,"type":"set_block","x":8,"y":7,"z":8,"id":57,"meta":0}' \
	'{"tick":0,"type":"set_inventory","slot":0,"item":276,"count":1,"meta":12}' \
	'{"tick":0,"type":"set_weather","raining":1,"thundering":1,"rain_time":100,"thunder_time":200}' \
	>/tmp/magma-state-hooks.jsonl
./magma_game --world superflat --headless --ticks 1 --script /tmp/magma-state-hooks.jsonl \
	--state-out /tmp/magma-state-hooks-state.jsonl --render off --pace unlimited
uv run --no-project python - <<'PY'
import json
row = json.loads(open("/tmp/magma-state-hooks-state.jsonl", encoding="utf-8").read())
assert row["look"]["id"] == 57
assert row["inventory"][0] == {
    "slot": 0, "item": 276, "count": 1, "meta": 12, "enchants": []}
assert row["weather"]["raining"] == 1
assert row["weather"]["thundering"] == 1
assert row["weather"]["rain_time"] == 99
assert row["weather"]["thunder_time"] == 199
assert row["weather"]["clean_weather_time"] == 0
assert row["weather"]["weather_cycle"] == 1
assert row["weather"]["daylight_cycle"] == 1
assert row["weather"]["prev_rain_strength"] == 1.0
assert row["weather"]["rain_strength"] == 1.0
assert row["weather"]["prev_thunder_strength"] == 1.0
assert row["weather"]["thunder_strength"] == 1.0
PY

# Full saved weather-clock state: doWeatherCycle=false leaves timers fixed but
# still advances Java's current/previous fade strengths; daylight is frozen.
printf '%s\n' \
	'{"tick":0,"type":"set_time","value":6000}' \
	'{"tick":0,"type":"set_total_time","value":42}' \
	'{"tick":0,"type":"set_daylight_cycle","enabled":0}' \
	'{"tick":0,"type":"set_weather","raining":1,"thundering":0,"rain_time":50,"thunder_time":100,"clean_weather_time":7,"weather_cycle":0,"prev_rain_strength":0.35,"rain_strength":0.36,"prev_thunder_strength":0.1,"thunder_strength":0.09}' \
	>/tmp/magma-weather-clock-state.jsonl
./magma_game --world superflat --headless --ticks 1 \
	--script /tmp/magma-weather-clock-state.jsonl \
	--state-out /tmp/magma-weather-clock-state-out.jsonl --render off --pace unlimited
uv run --no-project python - <<'PY'
import json
row = json.loads(open(
    "/tmp/magma-weather-clock-state-out.jsonl", encoding="utf-8").read())
weather = row["weather"]
assert row["world_time"] == 6000 and row["total_time"] == 43, row
assert weather["rain_time"] == 50 and weather["thunder_time"] == 100, weather
assert weather["clean_weather_time"] == 7, weather
assert weather["weather_cycle"] == 0 and weather["daylight_cycle"] == 0, weather
assert abs(weather["prev_rain_strength"] - 0.36) < 1e-7, weather
assert abs(weather["rain_strength"] - 0.37) < 1e-7, weather
assert abs(weather["prev_thunder_strength"] - 0.09) < 1e-7, weather
assert abs(weather["thunder_strength"] - 0.08) < 1e-7, weather
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
	>/tmp/magma-inv-click.jsonl
./magma_game --world superflat --headless --ticks 5 --script /tmp/magma-inv-click.jsonl \
	--state-out /tmp/magma-inv-click-state.jsonl --render off --pace unlimited
uv run --no-project python - <<'PY'
import json
rows = [json.loads(l) for l in open("/tmp/magma-inv-click-state.jsonl", encoding="utf-8")]
after_grid = rows[2]
assert after_grid["grid"][0] == [17, 1, 0], after_grid["grid"]
assert after_grid["craft_result"] == [5, 4, 0], after_grid["craft_result"]
final = rows[-1]
assert final["grid"][0] == [0, 0, 0]
assert any(s["item"] == 5 and s["count"] == 4 for s in final["inventory"])
assert any(s["item"] == 17 and s["count"] == 1 for s in final["inventory"])
PY
printf '%s\n' \
	'{"tick":0,"type":"action","inv_slot":90,"inv_button":0,"inv_type":0}' \
	>/tmp/magma-invalid-click.jsonl
if ./magma_game --world superflat --headless --ticks 1 --script /tmp/magma-invalid-click.jsonl \
	--render off --pace unlimited >/tmp/magma-invalid-click.out 2>&1; then
	echo "out-of-range inv_slot unexpectedly succeeded" >&2
	exit 1
fi
rg -q 'invalid inv_slot' /tmp/magma-invalid-click.out

printf '%s\n' \
	'{"tick":0,"type":"set_inventory","slot":0,"item":0,"count":1,"meta":0}' \
	>/tmp/magma-invalid-state-hook.jsonl
if ./magma_game --headless --ticks 1 --script /tmp/magma-invalid-state-hook.jsonl \
	--render off --pace unlimited >/tmp/magma-invalid-state-hook.out 2>&1; then
	echo "invalid inventory injection unexpectedly succeeded" >&2
	exit 1
fi
rg -q 'invalid set_inventory' /tmp/magma-invalid-state-hook.out
# Cold state-capsule hooks restore hidden FoodStats scalars and the selected
# hotbar slot before tick zero. They are setup-only and never alter the hot loop.
printf '%s\n' \
	'{"tick":0,"type":"set_food_stats","saturation":3.5,"exhaustion":0.25}' \
	'{"tick":0,"type":"set_inventory","slot":2,"item":1,"count":3,"meta":0}' \
	'{"tick":0,"type":"set_selected_slot","slot":2}' \
	'{"tick":0,"type":"set_total_time","value":42}' \
	'{"tick":0,"type":"schedule_tick","x":8,"y":3,"z":8,"block":1,"time":44,"priority":2,"order":17}' \
	>/tmp/magma-capsule-player.jsonl
for dz in $(seq -4 4); do
	for dx in $(seq -4 4); do
		if [ $((dx < 0 ? -dx : dx)) -le $((4 - (dz < 0 ? -dz : dz))) ]; then
			for yid in "2 1" "3 0" "4 0"; do
				read -r y id <<<"$yid"
				printf '{"tick":0,"type":"snapshot_block","x":%d,"y":%d,"z":%d,"id":%d,"meta":0}\n' \
					"$((10 + dx))" "$y" "$((8 + dz))" "$id" \
					>>/tmp/magma-capsule-player.jsonl
			done
		fi
	done
done
printf '%s\n' \
	'{"tick":0,"type":"snapshot_block","x":10,"y":3,"z":8,"id":8,"meta":0}' \
	'{"tick":0,"type":"schedule_tick","x":10,"y":3,"z":8,"block":8,"time":45,"priority":0,"order":18}' \
	>>/tmp/magma-capsule-player.jsonl
MAGMA_BLOCKS_OUT=/tmp/magma-capsule-player-blocks.bin \
MAGMA_BLOCKS_BOX=9,2,7,11,3,9 \
./magma_game --world superflat --headless --ticks 5 \
	--script /tmp/magma-capsule-player.jsonl \
	--state-out /tmp/magma-capsule-player-state.jsonl --render off --pace unlimited
uv run --no-project python - <<'PY'
import json
import struct
rows = [json.loads(line) for line in open(
    "/tmp/magma-capsule-player-state.jsonl", encoding="utf-8")]
row = rows[0]
assert row["saturation"] == 3.5, row
assert row["held_slot"] == 2 and row["held_id"] == 1
assert row["held_count"] == 3 and row["held_meta"] == 0
assert len(row["scheduled_ticks"]) == 2, row["scheduled_ticks"]
assert len(rows[1]["scheduled_ticks"]) == 1, rows[1]["scheduled_ticks"]
assert len(rows[2]["scheduled_ticks"]) == 5, rows[2]["scheduled_ticks"]
assert rows[-1]["scheduled_ticks"] == rows[2]["scheduled_ticks"]
raw = open("/tmp/magma-capsule-player-blocks.bin", "rb").read()
assert len(raw) == 36
cells = struct.unpack("<18H", raw)
def cell(x, y, z):
    return cells[((y - 2) * 3 + (z - 7)) * 3 + (x - 9)]
assert cell(10, 3, 7) == (8 << 4 | 1)
assert cell(9, 3, 8) == (8 << 4 | 1)
assert cell(11, 3, 8) == (8 << 4 | 1)
assert cell(10, 3, 9) == (8 << 4 | 1)
PY
# Drowning is applied after the ordinary EntityLivingBase timer decrement.
# A new hit therefore exposes hurtTime=10 on its own post-tick row, followed
# by an independent one-per-tick countdown (hurtResistantTime remains 20).
printf '%s\n' \
	'{"tick":0,"type":"set_pose_state","x":8.5,"y":200,"z":8.5,"yaw":0,"pitch":0,"vx":0,"vy":0,"vz":0,"on_ground":0,"fall":0}' \
	'{"tick":0,"type":"snapshot_block","x":8,"y":201,"z":8,"id":9,"meta":0}' \
	'{"tick":0,"type":"set_air","air":-19}' \
	>/tmp/magma-drowning-timer.jsonl
./magma_game --world superflat --headless --ticks 3 \
	--script /tmp/magma-drowning-timer.jsonl \
	--state-out /tmp/magma-drowning-timer-state.jsonl --render off --pace unlimited
uv run --no-project python - <<'PY'
import json
rows = [json.loads(line) for line in open(
    "/tmp/magma-drowning-timer-state.jsonl", encoding="utf-8")]
assert [(row["air"], row["health"], row["hurt_time"]) for row in rows] == [
    (0, 18.0, 9),
    (-1, 18.0, 8),
    (-2, 18.0, 7),
], rows
PY
printf '%s\n' \
	'{"tick":0,"type":"set_pose","x":8.5,"y":5,"z":8.5,"yaw":0,"pitch":0}' \
	'{"tick":0,"type":"spawn_entity","entity":2,"x":8.5,"y":5,"z":14.5}' \
	>/tmp/magma-entity-hook.jsonl
./magma_game --world superflat --headless --ticks 1 --script /tmp/magma-entity-hook.jsonl \
	--state-out /tmp/magma-entity-hook-state.jsonl --render off --pace unlimited
uv run --no-project python - <<'PY'
import json
row = json.loads(open("/tmp/magma-entity-hook-state.jsonl", encoding="utf-8").read())
assert any(entity["type"] == 2 and entity["health"] == 20 for entity in row["entities"])
PY
# Exact EntityItem sidecar injection used by Java-vs-magma plate fixtures.
# The parser must preserve authoritative identity/stack/timers, and the
# stationary variant must age without gravity or pickup-delay decrement.
printf '%s\n' \
	'{"tick":0,"type":"set_pose","x":8.5,"y":5,"z":8.5,"yaw":0,"pitch":0}' \
	'{"tick":0,"type":"spawn_item_fixture","eid":4022,"x":12.5,"y":100,"z":8.5,"vx":0,"vy":0,"vz":0,"item":1,"count":2,"meta":3,"age":7,"pickup_delay":32767,"controlled_stationary":1}' \
	>/tmp/magma-item-fixture.jsonl
./magma_game --world superflat --headless --ticks 1 --mobs off \
	--script /tmp/magma-item-fixture.jsonl \
	--state-out /tmp/magma-item-fixture-state.jsonl --render off --pace unlimited
uv run --no-project python - <<'PY'
import json
row = json.loads(open(
    "/tmp/magma-item-fixture-state.jsonl", encoding="utf-8").read())
items = [entity for entity in row["entities"] if entity["kind"] == "item"]
assert items == [{
    "kind": "item", "eid": 4022, "type": 0,
    "x": 12.5, "y": 100, "z": 8.5,
    "vx": 0, "vy": 0, "vz": 0,
    "yaw": 0, "pitch": 0, "health": 5,
    "item": 1, "count": 2, "meta": 3,
    "age": 8, "pickup_delay": 32767,
}], items
PY
# Exact EntityTippedArrow sidecar injection used by the wooden-button oracle.
# The controlled fixture keeps authoritative identity/pose and disables
# gravity/motion so a 30-tick button callback can query the same AABB.
printf '%s\n' \
	'{"tick":0,"type":"set_pose","x":8.5,"y":5,"z":8.5,"yaw":0,"pitch":0}' \
	'{"tick":0,"type":"spawn_arrow_fixture","eid":4023,"x":12.5,"y":100,"z":8.5,"vx":0,"vy":0,"vz":0,"yaw":0,"pitch":0,"controlled_stationary":1}' \
	>/tmp/magma-arrow-fixture.jsonl
./magma_game --world superflat --headless --ticks 1 --mobs off \
	--script /tmp/magma-arrow-fixture.jsonl \
	--state-out /tmp/magma-arrow-fixture-state.jsonl --render off --pace unlimited
uv run --no-project python - <<'PY'
import json
row = json.loads(open(
    "/tmp/magma-arrow-fixture-state.jsonl", encoding="utf-8").read())
arrows = [entity for entity in row["entities"]
          if entity["kind"] == "projectile"]
assert arrows == [{
    "kind": "projectile", "eid": 4023, "type": 1,
    "x": 12.5, "y": 100, "z": 8.5,
    "vx": 0, "vy": 0, "vz": 0,
    "yaw": 0, "pitch": 0, "health": -1,
}], arrows
PY
# Acceleration is part of EntitySmallFireball state, but not arrow state.
printf '%s\n' \
	'{"tick":0,"type":"set_pose","x":8.5,"y":5,"z":8.5,"yaw":0,"pitch":0}' \
	'{"tick":0,"type":"spawn_small_fireball_fixture","eid":4024,"x":12.5,"y":100,"z":8.5,"vx":0,"vy":0,"vz":0,"ax":0.01,"ay":-0.02,"az":0.03}' \
	>/tmp/magma-small-fireball-fixture.jsonl
./magma_game --world superflat --headless --ticks 1 --mobs off \
	--script /tmp/magma-small-fireball-fixture.jsonl \
	--state-out /tmp/magma-small-fireball-fixture-state.jsonl \
	--render off --pace unlimited
uv run --no-project python - <<'PY'
import json
row = json.loads(open(
    "/tmp/magma-small-fireball-fixture-state.jsonl", encoding="utf-8").read())
fireballs = [entity for entity in row["entities"]
             if entity["kind"] == "projectile"]
assert len(fireballs) == 1, fireballs
assert fireballs[0]["type"] == 3, fireballs
assert fireballs[0]["ax"] == 0.01, fireballs
assert fireballs[0]["ay"] == -0.02, fireballs
assert fireballs[0]["az"] == 0.03, fireballs
PY
rm -rf /tmp/magma-script-frames-a /tmp/magma-script-frames-b
printf '%s\n' \
	'{"tick":0,"type":"set_time","value":0}' \
	'{"tick":1,"type":"set_time","value":12000}' \
	>/tmp/magma-frame-time.jsonl
for dir in /tmp/magma-script-frames-a /tmp/magma-script-frames-b; do
	./magma_game --world superflat --view-distance 1 --width 160 --height 90 \
		--headless --ticks 2 --script /tmp/magma-frame-time.jsonl \
		--render off --pace unlimited --frames-out "$dir" \
		--state-out "$dir.jsonl"
done
cmp /tmp/magma-script-frames-a/frame_000000.ppm /tmp/magma-script-frames-b/frame_000000.ppm
cmp /tmp/magma-script-frames-a/frame_000001.ppm /tmp/magma-script-frames-b/frame_000001.ppm
cmp /tmp/magma-script-frames-a.jsonl /tmp/magma-script-frames-b.jsonl
if cmp -s /tmp/magma-script-frames-a/frame_000000.ppm \
	/tmp/magma-script-frames-a/frame_000001.ppm; then
	echo "day and night frame captures unexpectedly match" >&2
	exit 1
fi
uv run --no-project python - <<'PY'
import json
from pathlib import Path

frames = sorted(Path("/tmp/magma-script-frames-a").glob("frame_*.ppm"))
assert [frame.name for frame in frames] == ["frame_000000.ppm", "frame_000001.ppm"]
assert all(frame.read_bytes().startswith(b"P6\n160 90\n255\n") for frame in frames)
states = [json.loads(line) for line in Path("/tmp/magma-script-frames-a.jsonl").read_text().splitlines()]
assert [state["world_time"] for state in states] == [1, 12001]
PY
echo "script route: PASS"
