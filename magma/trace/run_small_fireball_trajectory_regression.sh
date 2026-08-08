#!/usr/bin/env bash
# Compare a real Java EntitySmallFireball save-state trajectory against magma.
set -euo pipefail

MAGMA="$(cd "$(dirname "$0")/.." && pwd)"
cd "$MAGMA"

OUT="${OUT:-trace/out/small_fireball_trajectory_regression}"
QRL_HOST="${QRL_HOST:-127.0.0.1}"
QRL_PORT="${QRL_PORT:-25575}"

if ! timeout 2 bash -c 'exec 3<>"/dev/tcp/$1/$2"' \
        _ "$QRL_HOST" "$QRL_PORT" 2>/dev/null; then
    echo "small-fireball regression requires the Java oracle at $QRL_HOST:$QRL_PORT" >&2
    exit 1
fi

env \
    TICKS=8 \
    SEED=0 \
    TAPE_PROFILE=idle \
    FRESH=1 \
    SMALL_FIREBALL_FIXTURE="6 8 6 0 0 0 0.03 0.01 0.08" \
    QRL_HOST="$QRL_HOST" \
    QRL_PORT="$QRL_PORT" \
    OUT="$OUT" \
    bash trace/run_oracle.sh

echo "PASS: Java and magma small-fireball position, motion, and acceleration match"
