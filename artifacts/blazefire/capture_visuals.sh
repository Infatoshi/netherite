#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$ROOT/artifacts/blazefire"
TMP_BASE=/home/infatoshi/dev/nw/.tmp
UV_CACHE_DIR=/home/infatoshi/.cache/uv
export UV_CACHE_DIR TMPDIR="$TMP_BASE"
mkdir -p "$TMP_BASE" "$OUT"
WORK="$(mktemp -d "$TMP_BASE/blazefire-visual.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT

make_blaze_script() {
    local script=$1
    printf '%s\n' \
        '{"tick":0,"type":"set_dimension","dimension":-1}' \
        '{"tick":0,"type":"set_pose","x":8.5,"y":65.0,"z":8.5,"yaw":0.0,"pitch":0.0}' \
        >"$script"
    for x in $(seq 5 11); do
        for z in $(seq 7 20); do
            printf '{"tick":0,"type":"set_block","x":%d,"y":64,"z":%d,"id":112,"meta":0}\n' "$x" "$z" >>"$script"
            for y in $(seq 65 69); do
                printf '{"tick":0,"type":"set_block","x":%d,"y":%d,"z":%d,"id":0,"meta":0}\n' "$x" "$y" "$z" >>"$script"
            done
        done
    done
    for y in $(seq 65 68); do
        printf '{"tick":0,"type":"set_block","x":5,"y":%d,"z":15,"id":112,"meta":0}\n' "$y" >>"$script"
        printf '{"tick":0,"type":"set_block","x":11,"y":%d,"z":15,"id":112,"meta":0}\n' "$y" >>"$script"
    done
    for x in $(seq 5 11); do
        printf '{"tick":0,"type":"set_block","x":%d,"y":69,"z":15,"id":112,"meta":0}\n' "$x" >>"$script"
    done
    printf '%s\n' \
        '{"tick":0,"type":"set_block","x":8,"y":66,"z":12,"id":20,"meta":0}' \
        '{"tick":1,"type":"spawn_entity","entity":7,"x":8.5,"y":65.0,"z":16.5}' \
        '{"tick":2,"type":"set_block","x":8,"y":66,"z":12,"id":0,"meta":0}' \
        >>"$script"
}

make_zombie_script() {
    local script=$1
    printf '%s\n' \
        '{"tick":0,"type":"set_pose","x":8.5,"y":5.0,"z":8.5,"yaw":0.0,"pitch":0.0}' \
        '{"tick":0,"type":"set_time","value":1000}' \
        '{"tick":0,"type":"spawn_entity","entity":2,"x":8.5,"y":5.0,"z":14.5}' \
        >"$script"
}

run_scene() {
    local script=$1 frames=$2 ticks=$3
    mkdir -p "$frames"
    if ! SDL_VIDEODRIVER=dummy "$ROOT/magma/magma_game" \
        --world superflat --mobs on --headless --ticks "$ticks" \
        --script "$script" --frames-out "$frames" --compose window \
        --width 854 --height 480 --view-distance 1 --set hide_gui=1 \
        >"$frames/state.jsonl" 2>"$frames/run.log"; then
        cat "$frames/run.log" >&2
        return 1
    fi
}

make_blaze_script "$WORK/blaze.jsonl"
make_zombie_script "$WORK/zombie.jsonl"
run_scene "$WORK/blaze.jsonl" "$WORK/blaze_frames" 3
run_scene "$WORK/zombie.jsonl" "$WORK/zombie_frames" 1

UV_CACHE_DIR="$UV_CACHE_DIR" TMPDIR="$TMP_BASE" \
    uv run --no-project --with numpy,scipy,pillow,nbt python - \
    "$WORK/blaze_frames/frame_000001.ppm" "$OUT/idle_blaze_not_burning.png" \
    "$WORK/blaze_frames/frame_000002.ppm" "$OUT/attacking_blaze_burning.png" \
    "$WORK/zombie_frames/frame_000000.ppm" "$OUT/daylight_zombie_burning.png" <<'PY'
from pathlib import Path
import sys
from PIL import Image

for source, target in zip(sys.argv[1::2], sys.argv[2::2]):
    image = Image.open(source)
    image.save(target)
    print(f"{Path(target).name}: {image.size[0]}x{image.size[1]}")
PY
