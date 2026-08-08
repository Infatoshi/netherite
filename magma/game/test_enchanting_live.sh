#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
make game/test_enchanting_live
./game/test_enchanting_live
uv run --no-project python ../blaze/oracle/runner.py enchant_table
