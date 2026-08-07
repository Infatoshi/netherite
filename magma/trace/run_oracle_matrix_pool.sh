#!/usr/bin/env bash
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
POOL="$REPO/java/start_oracle_instance.sh"
INSTANCES="${ORACLE_MATRIX_INSTANCES:-32}"
CASE_TIMEOUT="${ORACLE_MATRIX_CASE_TIMEOUT:-180}"

if ! [[ "$INSTANCES" =~ ^[0-9]+$ ]] \
   || [ "$INSTANCES" -lt 1 ] || [ "$INSTANCES" -gt 32 ]; then
    echo "ORACLE_MATRIX_INSTANCES must be an integer in 1..32" >&2
    exit 2
fi
if ! [[ "$CASE_TIMEOUT" =~ ^[0-9]+$ ]] || [ "$CASE_TIMEOUT" -lt 1 ]; then
    echo "ORACLE_MATRIX_CASE_TIMEOUT must be a positive integer" >&2
    exit 2
fi

cleanup() {
    local instance
    for ((instance = 0; instance < INSTANCES; ++instance)); do
        bash "$POOL" stop "$instance" &
    done
    wait
}
trap cleanup EXIT INT TERM

UV_CACHE_DIR=/home/jawaugh/.cache/uv \
TMPDIR="$REPO/.tmp" \
JAVA_HOME=/usr/lib/jvm/java-8-openjdk-amd64 \
uv run --no-project python "$HERE/run_oracle_matrix.py" \
    --start-pool --instances "$INSTANCES" \
    --case-timeout "$CASE_TIMEOUT" "$@"
