#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
exec uv run --no-project python \
    "$ROOT/verify/completeness/crafting_registry_gate.py"
