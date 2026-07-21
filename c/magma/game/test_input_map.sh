#!/usr/bin/env bash
# Standalone build+run of the input_map unit test (no Makefile involvement).
# Run from the magma project root: bash game/test_input_map.sh
set -euo pipefail
cd "$(dirname "$0")/.."
gcc -O2 -ffp-contract=off -Wall -Wextra -I. -Icore \
    game/input_map.c game/test_input_map.c -o game/test_input_map -lm
./game/test_input_map
