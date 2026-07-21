#!/usr/bin/env bash
# Standalone build+run of the look/move consistency test (no Makefile involvement).
# Run from the magma project root: bash game/test_view.sh
set -euo pipefail
cd "$(dirname "$0")/.."
gcc -O2 -ffp-contract=off -Wall -Wextra -I. -Icore \
    core/math.c game/input_map.c game/test_view.c -o game/test_view -lm
./game/test_view
