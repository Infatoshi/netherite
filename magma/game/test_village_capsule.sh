#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$ROOT/.tmp"
TEMP="$(mktemp -d "$ROOT/.tmp/village-capsule.XXXXXX")"
trap 'rm -rf "$TEMP"' EXIT

cd "$ROOT"
make game >/dev/null

SCRIPT="$TEMP/events.jsonl"
STATE="$TEMP/state.jsonl"
printf '%s\n' \
    '{"tick":0,"type":"set_block","x":0,"y":200,"z":0,"id":64,"meta":0}' \
    '{"tick":0,"type":"set_world_random_seed","value":20015998343868}' \
    '{"tick":0,"type":"restore_village_collection","collection_tick":41,"count":1}' \
    '{"tick":0,"type":"restore_village_state","index":0,"population":1,"radius":32,"golems":0,"stable":40,"state_tick":41,"mating_tick":0,"center_x":0,"center_y":200,"center_z":0,"helper_x":0,"helper_y":200,"helper_z":0}' \
    '{"tick":0,"type":"restore_village_door","index":0,"x":0,"y":200,"z":0,"inside_dx":2,"inside_dz":0,"timestamp":40}' \
    '{"tick":0,"type":"restore_village_reputation","index":0,"most_hi":19088743,"most_lo":2309737967,"least_hi":4275878552,"least_lo":1985229328,"score":-7}' \
    >"$SCRIPT"

./magma_game --world superflat --weather off --daylight off --mobs off \
    --headless --ticks 2 --script "$SCRIPT" --state-out "$STATE" \
    --render off --pace unlimited >/dev/null

uv run --no-project python - "$STATE" <<'PY'
import json
import sys

rows = [json.loads(line) for line in open(sys.argv[1], encoding="utf-8")]
assert len(rows) == 2

seed = 20015998343868
expected_seeds = []
for _ in range(2):
    while True:
        seed = (seed * 0x5DEECE66D + 0xB) & ((1 << 48) - 1)
        bits = seed >> 17
        value = bits % 50
        if bits - value + 49 < (1 << 31):
            break
    expected_seeds.append(seed)

for offset, row in enumerate(rows, 1):
    collection = row["village_collection"]
    assert collection["tick"] == 41 + offset
    assert row["world_rand_seed48"] == expected_seeds[offset - 1]
    assert collection["villages"] == [{
        "population": 1,
        "radius": 32,
        "golems": 0,
        "stable": 40,
        "state_tick": 41 + offset,
        "mating_tick": 0,
        "center_x": 0,
        "center_y": 200,
        "center_z": 0,
        "helper_x": 0,
        "helper_y": 200,
        "helper_z": 0,
        "doors": [{
            "x": 0,
            "y": 200,
            "z": 0,
            "inside_dx": 2,
            "inside_dz": 0,
            "timestamp": 40,
        }],
        "reputations": [{
            "uuid_most_hex": "0123456789abcdef",
            "uuid_least_hex": "fedcba9876543210",
            "score": -7,
        }],
    }]

print("village capsule runtime: PASS (restore, tick order, RNG, persistence)")
PY
