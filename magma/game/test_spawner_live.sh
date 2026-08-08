#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
export JAVA_HOME=${JAVA_HOME:-/usr/lib/jvm/java-8-openjdk-amd64}
export PATH="$JAVA_HOME/bin:$PATH"
export TMPDIR=${TMPDIR:-"$PWD/.tmp"}
mkdir -p .tmp/spawner-live
make game/test_spawner_live >/dev/null
./game/test_spawner_live >.tmp/spawner-live/c.txt
(cd ../java/Minecraft && ./gradlew -g run/gradle spawnerGolden --quiet) \
    2>/dev/null | sed -n \
    -e '/^T[01CR] /p' \
    -e '/spawner_live:/s/.*spawner_live:/spawner_live:/p' \
    >.tmp/spawner-live/java.txt
cmp .tmp/spawner-live/java.txt .tmp/spawner-live/c.txt
make game >/dev/null 2>.tmp/spawner-live/build-game.err
printf '%s' \
    0a0000080002696400106d696e6563726166743a7a6f6d62696500 \
    | xxd -r -p >.tmp/spawner-live/zombie.nbt
export MAGMA_CAPSULE_DIR="$PWD/.tmp/spawner-live"
printf '%s\n' \
    '{"tick":0,"type":"set_gamerules","doMobSpawning":"false"}' \
    '{"tick":0,"type":"set_block","x":12,"y":78,"z":8,"id":52,"meta":0}' \
    '{"tick":0,"type":"restore_loaded_tile_order","order":0,"x":12,"y":78,"z":8}' \
    '{"tick":0,"type":"restore_tickable_tile_order","order":0,"x":12,"y":78,"z":8}' \
    '{"tick":0,"type":"set_spawner_state","dim":0,"x":12,"y":78,"z":8,"entity":2,"delay":20,"min_delay":7,"max_delay":11,"spawn_count":1,"max_nearby":6,"activate_range":16,"spawn_range":4,"spawn_nbt_file":"zombie.nbt","default_entity_nbt":true}' \
    '{"tick":0,"type":"add_spawner_potential","dim":0,"x":12,"y":78,"z":8,"entity":2,"weight":1,"entity_nbt_file":"zombie.nbt","default_entity_nbt":true}' \
    >.tmp/spawner-live/restore.jsonl
./magma_game --world superflat --headless --ticks 1 --mobs on \
    --script .tmp/spawner-live/restore.jsonl \
    --state-out .tmp/spawner-live/restore-state.jsonl \
    --render off --pace unlimited
uv run --no-project python - .tmp/spawner-live/restore-state.jsonl <<'PY'
import json
import sys
row = json.loads(open(sys.argv[1], encoding="utf-8").readline())
assert row["do_mob_spawning"] is False, row
assert row["loaded_tile_order"] == [[12, 78, 8]], row
assert row["tickable_tile_order"] == [[12, 78, 8]], row
assert row["spawners"] == [{
    "x": 12, "y": 78, "z": 8, "entity": 2, "delay": 20,
    "min_delay": 7, "max_delay": 11, "spawn_count": 1,
    "max_nearby": 6, "activate_range": 16, "spawn_range": 4,
    "spawn_data_nbt":
        "0a0000080002696400106d696e6563726166743a7a6f6d62696500",
    "default_entity_nbt": True,
    "potentials": [{
        "entity": 2, "weight": 1,
        "entity_nbt":
            "0a0000080002696400106d696e6563726166743a7a6f6d62696500",
        "default_entity_nbt": True,
    }],
}], row
PY
echo "spawner_oracle: PASS (real 1.11.2 block-spawner scalar/RNG order)"
