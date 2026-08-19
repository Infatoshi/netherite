#!/usr/bin/env bash
# Focused CPU matrix: blaze dig leftClickCounter (mirrors magma player_ctl).
set -euo pipefail
cd "$(dirname "$0")"
gcc -O2 -ffp-contract=off -Wall -Wextra -I. -I../core \
  test_left_click_counter.c -lm -o test_left_click_counter
./test_left_click_counter
