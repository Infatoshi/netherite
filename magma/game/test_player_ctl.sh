#!/usr/bin/env bash
# Standalone build+run of the player_ctl unit test (no Makefile involvement).
# Run from anywhere: bash game/test_player_ctl.sh
set -euo pipefail
cd "$(dirname "$0")/.."
BLAZE_CORE="$(cd "$(dirname "$0")/../../blaze/core" && pwd)"
gcc -O2 -ffp-contract=off -Wall -Wextra \
    -I. -Icore -I"$BLAZE_CORE" \
    game/player_ctl.c game/sel_box.c game/test_player_ctl.c -o game/test_player_ctl -lm
./game/test_player_ctl
