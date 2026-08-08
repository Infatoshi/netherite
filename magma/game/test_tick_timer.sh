#!/usr/bin/env bash
# Standalone build+run of the tick-timer unit test (no Makefile involvement).
# Run from anywhere: bash game/test_tick_timer.sh
set -euo pipefail
cd "$(dirname "$0")/.."
BLAZE_CORE="$(cd "$(dirname "$0")/../../blaze/core" && pwd)"
gcc -O2 -ffp-contract=off -Wall -Wextra \
    -I. -Icore -I"$BLAZE_CORE" \
    game/timer.c game/player_ctl.c game/sel_box.c game/test_tick_timer.c -o game/test_tick_timer -lm
./game/test_tick_timer
