#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
export TMPDIR=${TMPDIR:-"$ROOT/.tmp"}
mkdir -p "$TMPDIR"
cd "$ROOT/magma"
make game/test_scattered_live >/dev/null
./game/test_scattered_live
