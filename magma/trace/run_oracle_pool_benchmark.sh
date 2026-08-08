#!/usr/bin/env bash
# Run the scaling benchmark in one bounded cgroup and retire the hot pool.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MAGMA="$(cd "$HERE/.." && pwd)"
REPO="$(cd "$MAGMA/.." && pwd)"
MAX_INSTANCES="${ORACLE_POOL_MAX_INSTANCES:-32}"
SLICE="netherite-oracle.slice"

cleanup() {
    seq 0 $((MAX_INSTANCES - 1)) | xargs -P 16 -I{} \
        bash "$REPO/java/start_oracle_instance.sh" stop {} >/dev/null 2>&1 || true
}
trap cleanup EXIT

export JAVA_HOME="${JAVA_HOME:-/usr/lib/jvm/java-8-openjdk-amd64}"
export UV_CACHE_DIR="${UV_CACHE_DIR:-/home/jawaugh/.cache/uv}"
export TMPDIR="${TMPDIR:-$REPO/.tmp}"
mkdir -p "$TMPDIR"

systemctl --user set-property --runtime "$SLICE" \
    MemoryHigh=300G MemoryMax=350G

cd "$MAGMA"
systemd-run --user --scope --slice="$SLICE" \
    --property=MemoryHigh=300G --property=MemoryMax=350G \
    uv run --no-project python trace/benchmark_oracle_pool.py "$@"
