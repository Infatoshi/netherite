#!/usr/bin/env bash
# delegate_gate.sh TAPE_NAME [TAPE_NAME...] - the delegate acceptance test.
# Run from the root of your worktree. You are not done until this prints
# ACCEPT.
#
# 1. Tamper check: your diff vs merge-base with master must not touch gate
#    thresholds, divergence classes, known_divergences sidecars, or committed
#    gate baselines. A pass achieved by retuning is a lie and is rejected
#    mechanically.
# 2. Build: make -C magma game (CUDA parity is checked at merge time).
# 3. Replay + gate each TAPE_NAME (--cpu --report), rc must be 0.
# 4. Replay the pinned regression set (scripts/regression_pins.txt), rc 0.
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERIFY="$ROOT/verify"
PINS="$ROOT/scripts/regression_pins.txt"
export UV_CACHE_DIR=/home/infatoshi/.cache/uv TMPDIR=/home/infatoshi/dev/nw/.tmp
mkdir -p "$TMPDIR"
[ $# -ge 1 ] || { echo "usage: delegate_gate.sh TAPE_NAME..."; exit 2; }

fail() { echo "REJECT: $*"; exit 1; }

# --- 1. tamper check -------------------------------------------------------
base=$(git -C "$ROOT" merge-base HEAD master 2>/dev/null || git -C "$ROOT" merge-base HEAD origin/master)
touched=$( { git -C "$ROOT" diff --name-only "$base"...HEAD; git -C "$ROOT" diff --name-only; git -C "$ROOT" diff --name-only --cached; } | sort -u)
# report/*.gate.json is NOT frozen: replay_tape.py --report regenerates those
# tracked artifacts as a side effect of the required replays. rc comes from a
# fresh replay, so editing them cannot fake a pass; merges drop report diffs.
frozen='(known_divergences\.json$|verify/trace/pixel_gate\.py$|^GOAL\.md$|scripts/delegate_gate\.sh$|scripts/regression_pins\.txt$)'
bad=$(printf '%s\n' "$touched" | rg "$frozen" || true)
[ -z "$bad" ] || fail "diff touches frozen gate machinery:"$'\n'"$bad"

# --- 2. build --------------------------------------------------------------
make -C "$ROOT/magma" game -j"$(nproc)" >/dev/null || fail "build failed"

# --- 3+4. replays ----------------------------------------------------------
replay_one() {
    local tape="$1"
    local f="$VERIFY/tapes/${tape}.jsonl"
    [ -f "$f" ] || f="$tape"
    [ -f "$f" ] || fail "tape not found: $tape"
    echo "[gate] replaying $(basename "$f")"
    (cd "$VERIFY/trace" && uv run --no-project --with numpy --with scipy \
        --with pillow --with nbt python replay_tape.py "$f" --cpu --report) \
        || fail "gate rc!=0 on $(basename "$f")"
}
for t in "$@"; do replay_one "$t"; done
if [ -f "$PINS" ]; then
    while IFS= read -r p; do
        case "$p" in ''|\#*) continue;; esac
        replay_one "$p"
    done < "$PINS"
else
    echo "[gate] WARNING: no regression pins file"
fi
echo "ACCEPT"
