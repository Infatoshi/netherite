#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

out="${TMPDIR:-/tmp}/magma-test-furnace-live-$$"
trap 'rm -f "$out"' EXIT

"${CC:-cc}" -O2 -ffp-contract=off -Wall -Wextra -std=c11 -I. -I../blaze/core \
	game/test_furnace_live.c game/furnace_live.c -o "$out"
"$out"
