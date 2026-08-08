#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
${CC:-cc} -O2 -ffp-contract=off -Wall -Wextra -I. \
	game/test_block_registry.c -o game/test_block_registry
./game/test_block_registry
