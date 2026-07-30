#!/usr/bin/env bash
# Standalone build+run of the overlay geometry test (no Makefile involvement).
# Run from the magma project root: bash game/test_overlay.sh
set -euo pipefail
cd "$(dirname "$0")/.."
gcc -O2 -ffp-contract=off -Wall -Wextra -I. -Icore -I../blaze/core \
    game/overlay.c game/sel_box.c assets/blockmodels.c game/test_overlay.c core/math.c \
    -o game/test_overlay -lm
./game/test_overlay
