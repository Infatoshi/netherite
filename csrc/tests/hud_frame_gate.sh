#!/usr/bin/env bash
# Pixel gate for the portions of the new oracle frames drawn by raster_hud.
set -euo pipefail
cd "$(dirname "$0")/../.."
export TMPDIR="${TMPDIR:-$HOME/dev/nw/.tmp}"

make -s -C csrc "$PWD/out/native/raster" "$PWD/out/native/pxdiff"
px=(out/native/pxdiff)
"${px[@]}" selftest | tail -1

count25() {
    local out
    out=$("${px[@]}" probe --a "$1/golden.png" --b "$1/native.png" \
        --at 426,404 --size 360x54)
    if [[ "$out" =~ ([0-9]+)\ differing\ px ]]; then
        echo "${BASH_REMATCH[1]}"
    else
        echo 0
    fi
}

max_rgb() {
    "${px[@]}" stats --a "$1/golden.png" --b "$1/native.png" --rect "$2" \
        | jq '.max_abs_rgb | max'
}

for scene in hud_mixed hud_underwater; do
    dir="out/java/render/$scene"
    out/native/raster "$dir" "$dir/native.png" > "$dir/native.log" 2>&1
    "${px[@]}" survey --a "$dir/golden.png" --b "$dir/native.png" -o "$dir/survey" \
        > "$dir/survey.log"
    pixels=$(count25 "$dir")
    if [[ "$scene" == hud_mixed ]]; then
        flat=$(max_rgb "$dir" 246,437,406,477)
        potion=$(max_rgb "$dir" 526,437,562,477)
        egg=$(max_rgb "$dir" 566,437,602,477)
        (( pixels <= 3 && flat <= 3 && potion <= 1 && egg <= 1 ))
        echo "PASS $scene: bar pixels at threshold 25=$pixels, flat max=$flat, potion max=$potion, egg max=$egg"
    else
        (( pixels == 0 ))
        echo "PASS $scene: bar pixels at threshold 25=$pixels"
    fi
done
