#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
make game >/dev/null
rain_script=/tmp/magma-weather-rain-wet.jsonl
clear_script=/tmp/magma-weather-clear-wet.jsonl
rain_state=/tmp/magma-weather-rain-wet-state.jsonl
clear_state=/tmp/magma-weather-clear-wet-state.jsonl
printf '%s\n' \
  '{"tick":0,"type":"set_pose","x":32.5,"y":100,"z":32.5,"yaw":0,"pitch":0}' \
  '{"tick":0,"type":"set_weather","raining":1,"thundering":0,"rain_time":100,"thunder_time":100}' \
  '{"tick":0,"type":"set_fire","fire":42}' >"$rain_script"
printf '%s\n' \
  '{"tick":0,"type":"set_pose","x":32.5,"y":100,"z":32.5,"yaw":0,"pitch":0}' \
  '{"tick":0,"type":"set_weather","raining":0,"thundering":0,"rain_time":100,"thunder_time":100}' \
  '{"tick":0,"type":"set_fire","fire":42}' >"$clear_script"
./magma_game --seed 0 --weather on --mobs off --view-distance 1 --headless --ticks 1 \
  --script "$rain_script" --state-out "$rain_state" --render off --pace unlimited >/dev/null
./magma_game --seed 0 --weather on --mobs off --view-distance 1 --headless --ticks 1 \
  --script "$clear_script" --state-out "$clear_state" --render off --pace unlimited >/dev/null
uv run --no-project python - "$rain_state" "$clear_state" <<'PY'
import json,sys
rain=json.loads(open(sys.argv[1],encoding="utf-8").readline())
clear=json.loads(open(sys.argv[2],encoding="utf-8").readline())
assert rain["fire"] == -20, rain["fire"]
assert clear["fire"] == 41, clear["fire"]
print("weather_runtime: PASS (open-sky rain extinguishes; clear control burns)")
PY
