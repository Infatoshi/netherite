#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
make game
mkdir -p .tmp
SCRIPT=.tmp/test-armor-stand-script.jsonl
STATE=.tmp/test-armor-stand-state.jsonl

cat >"$SCRIPT" <<'EOF'
{"tick":0,"type":"spawn_armor_stand_fixture","eid":0,"x":8.5,"y":5,"z":8.5,"vx":0.125,"vy":-0.25,"vz":-0.375,"yaw":27.5,"pitch":-4.25,"health":12.5,"on_ground":1,"no_gravity":1,"invisible":1,"status":13,"disabled_slots":66052,"ticks_existed":17,"fire":3,"punch_cooldown":9}
{"tick":0,"type":"set_armor_stand_living_state","eid":0,"air":299,"in_water":0,"fall_distance":0.5,"hurt_time":2,"death_time":0,"hurt_resistant_time":3,"last_damage":1.25}
{"tick":0,"type":"set_armor_stand_generic_state","eid":0,"absorption":2.5,"max_health":30,"max_health_base":30,"revenge_timer":31,"portal_cooldown":7,"name_visible":1,"silent":1,"glowing":1,"invulnerable":1,"update_blocked":0,"fall_flying":0,"vehicle_eid":-1}
{"tick":0,"type":"set_armor_stand_custom_name","eid":0,"name":"Sentinel"}
{"tick":0,"type":"add_armor_stand_tag","eid":0,"tag":"guard"}
{"tick":0,"type":"add_armor_stand_tag","eid":0,"tag":"west"}
{"tick":0,"type":"add_armor_stand_effect","eid":0,"id":10,"amplifier":1,"duration":50,"ambient":0,"show_particles":1}
{"tick":0,"type":"set_armor_stand_uuid","eid":0,"most":-123456789,"least":987654321}
{"tick":0,"type":"set_armor_stand_random_state","eid":0,"entity_seed48":20015998343868,"entity_have_gaussian":1,"entity_gaussian":-0.375}
{"tick":0,"type":"set_armor_stand_pose","eid":0,"part":0,"x":1,"y":2,"z":3}
{"tick":0,"type":"set_armor_stand_pose","eid":0,"part":1,"x":11,"y":12,"z":13}
{"tick":0,"type":"set_armor_stand_pose","eid":0,"part":2,"x":21,"y":22,"z":23}
{"tick":0,"type":"set_armor_stand_pose","eid":0,"part":3,"x":31,"y":32,"z":33}
{"tick":0,"type":"set_armor_stand_pose","eid":0,"part":4,"x":41,"y":42,"z":43}
{"tick":0,"type":"set_armor_stand_pose","eid":0,"part":5,"x":51,"y":52,"z":53}
{"tick":0,"type":"set_armor_stand_equipment","eid":0,"slot":0,"item":276,"count":1,"meta":73,"n_ench":1,"e0":1048581,"repair_cost":7,"custom_name":"Stand Blade"}
{"tick":0,"type":"set_armor_stand_equipment","eid":0,"slot":5,"item":310,"count":1,"meta":19}
{"tick":0,"type":"ent_view","ent":"EntityArmorStand","id":7001,"x":8.5,"y":5,"z":8.5,"yaw":27.5,"armor_feet":301,"armor_legs":300,"armor_chest":299,"armor_head":298,"armor_color_valid":15,"armor_color_0":1056816,"armor_color_1":4214880,"armor_color_2":7372944,"armor_color_3":10531008,"stand_mainhand":276,"stand_offhand":261,"stand_flags":7,"stand_pose_valid":1,"stand_punch_time":2.25,"stand_pose_0_x":1,"stand_pose_0_y":2,"stand_pose_0_z":3,"stand_pose_1_x":11,"stand_pose_1_y":12,"stand_pose_1_z":13,"stand_pose_2_x":21,"stand_pose_2_y":22,"stand_pose_2_z":23,"stand_pose_3_x":31,"stand_pose_3_y":32,"stand_pose_3_z":33,"stand_pose_4_x":41,"stand_pose_4_y":42,"stand_pose_4_z":43,"stand_pose_5_x":51,"stand_pose_5_y":52,"stand_pose_5_z":53}
EOF

./magma_game --world superflat --headless --ticks 1 --mobs off \
    --script "$SCRIPT" --state-out "$STATE" --render off --pace unlimited

uv run --no-project python - "$STATE" <<'PY'
import json
import sys

row = json.loads(open(sys.argv[1], encoding="utf-8").readline())
stand = next(entity for entity in row["entities"]
             if entity["kind"] == "armor_stand")
assert stand["eid"] == 0
assert (stand["x"], stand["y"], stand["z"]) == (8.5, 5, 8.5)
assert (stand["vx"], stand["vy"], stand["vz"]) == (
    0.1225, -0.245, -0.3675)
assert stand["armor_stand_exact"] is True
assert stand["armor_stand_small"] is True
assert stand["armor_stand_show_arms"] is True
assert stand["armor_stand_no_base_plate"] is True
assert stand["armor_stand_marker"] is False
assert stand["armor_stand_no_gravity"] is True
assert stand["armor_stand_invisible"] is True
assert stand["armor_stand_disabled_slots"] == 66052
assert stand["armor_stand_ticks_existed"] == 18
assert stand["armor_stand_fire"] == 2
assert stand["hurt_time"] == 1
assert stand["hurt_resistant_time"] == 2
assert stand["health"] == 13.5
assert stand["armor_stand_absorption"] == 2.5
assert stand["armor_stand_max_health"] == 30
assert stand["armor_stand_max_health_base"] == 30
assert stand["armor_stand_revenge_timer"] == 31
assert stand["armor_stand_portal_cooldown"] == 6
assert stand["armor_stand_custom_name"] == "Sentinel"
assert stand["armor_stand_custom_name_visible"] is True
assert stand["armor_stand_silent"] is True
assert stand["armor_stand_glowing"] is True
assert stand["armor_stand_invulnerable"] is True
assert stand["armor_stand_update_blocked"] is False
assert stand["armor_stand_fall_flying"] is False
assert stand["armor_stand_vehicle_eid"] == -1
assert stand["armor_stand_tags"] == ["guard", "west"]
assert stand["armor_stand_effects"] == [{
    "id": 10, "amp": 1, "dur": 49,
    "ambient": False, "show_particles": True,
}]
assert stand["uuid_most"] == -123456789
assert stand["uuid_least"] == 987654321
assert stand["armor_stand_entity_seed48"] == 20015998343868
assert stand["armor_stand_entity_have_gaussian"] is True
assert stand["armor_stand_entity_gaussian"] == -0.375
assert stand["armor_stand_pose"] == [
    [1, 2, 3], [11, 12, 13], [21, 22, 23],
    [31, 32, 33], [41, 42, 43], [51, 52, 53],
]
equipment = {stack["slot"]: stack for stack in stand["armor_stand_equipment"]}
assert equipment[0]["id"] == 276 and equipment[0]["meta"] == 73
assert equipment[0]["enchants"] == [[16, 5]]
assert equipment[0]["repair_cost"] == 7
assert equipment[0]["custom_name"] == "Stand Blade"
assert equipment[5]["id"] == 310 and equipment[5]["meta"] == 19
PY

echo "PASS: armor stand capsule script and state surface"
