#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
${CC:-cc} -O2 -ffp-contract=off -Wall -Wextra -I. \
	game/config.c game/test_config.c -o game/test_config
./game/test_config
