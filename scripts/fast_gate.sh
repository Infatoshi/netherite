#!/usr/bin/env bash
# fast_gate.sh TAPE_NAME [TAPE_NAME...] - parallel outer runner, verdict-equivalent
# to the frozen scripts/delegate_gate.sh.
#
# Does NOT edit the frozen gate. Same usage, same tamper check, same build,
# same replay command shape (verify/trace/replay_tape.py --cpu --report).
#
# Differences vs the serial frozen gate (verdict-preserving only):
#   - Each requested target is replayed once.
#   - Pins from scripts/regression_pins.txt that already appear as targets are
#     not replayed again (the frozen gate re-runs pin#1 when it is the target).
#   - Unique pin (and multi-target) replays run concurrently, capped at 6
#     concurrent workers total.
#
# Fail closed: nonzero rc, missing .gate.json report, frames_checked==0,
# or a killed/signaled worker => overall REJECT.
#
# Prints per-tape results, magma_game sha256, the deduplicated tape list, and
# final ACCEPT/REJECT with the same semantics as delegate_gate.sh (rc 0 only
# when every replay would have been accepted there, plus the harness checks
# above).
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERIFY="$ROOT/verify"
PINS="$ROOT/scripts/regression_pins.txt"
export UV_CACHE_DIR="${UV_CACHE_DIR:-$HOME/.cache/uv}" TMPDIR="${TMPDIR:-$HOME/dev/nw/.tmp}"
mkdir -p "$TMPDIR"
[ $# -ge 1 ] || { echo "usage: fast_gate.sh TAPE_NAME..."; exit 2; }

fail() { echo "REJECT: $*"; exit 1; }

MAX_CONCURRENT=6

# --- 1. tamper check (identical to delegate_gate.sh) -----------------------
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
BIN="$ROOT/magma/magma_game"
[ -x "$BIN" ] || fail "magma_game missing after build"
BIN_SHA=$(sha256sum "$BIN" | awk '{print $1}')
echo "[fast_gate] magma_game sha256=$BIN_SHA"

# --- 3. build unique ordered tape list (targets first, then remaining pins) -
# Preserve target order; drop duplicate targets. Then append pins not already
# requested, preserving pin-file order.
declare -a UNIQUE=()
declare -A SEEN=()
add_unique() {
    local t="$1"
    [ -n "$t" ] || return 0
    if [ -z "${SEEN[$t]+x}" ]; then
        SEEN[$t]=1
        UNIQUE+=("$t")
    fi
}
for t in "$@"; do add_unique "$t"; done
if [ -f "$PINS" ]; then
    while IFS= read -r p; do
        case "$p" in ''|\#*) continue;; esac
        add_unique "$p"
    done < "$PINS"
else
    echo "[fast_gate] WARNING: no regression pins file"
fi

echo "[fast_gate] unique tapes (${#UNIQUE[@]}): ${UNIQUE[*]}"

# --- 4. concurrent replays, cap MAX_CONCURRENT -----------------------------
LOGDIR="$TMPDIR/fast_gate_$$"
mkdir -p "$LOGDIR"
# Logs kept on REJECT for inspection; wiped only on ACCEPT.

resolve_tape() {
    local tape="$1"
    local f="$VERIFY/tapes/${tape}.jsonl"
    if [ -f "$f" ]; then
        printf '%s\n' "$f"
        return 0
    fi
    if [ -f "$tape" ]; then
        printf '%s\n' "$tape"
        return 0
    fi
    return 1
}

# Workers write: $LOGDIR/<safe>.log, $LOGDIR/<safe>.rc, $LOGDIR/<safe>.name
# safe = tape basename with path separators scrubbed.
safe_name() {
    local t="$1"
    printf '%s' "$t" | tr '/ ' '__'
}

run_one() {
    local tape="$1"
    local safe
    safe=$(safe_name "$tape")
    local log="$LOGDIR/${safe}.log"
    local rcfile="$LOGDIR/${safe}.rc"
    local f
    if ! f=$(resolve_tape "$tape"); then
        echo "tape not found: $tape" >"$log"
        echo 127 >"$rcfile"
        printf '%s\n' "$tape" >"$LOGDIR/${safe}.name"
        return 0
    fi
    printf '%s\n' "$tape" >"$LOGDIR/${safe}.name"
    echo "[fast_gate] replaying $(basename "$f") (log=$log)"
    (
        cd "$VERIFY/trace" && uv run --no-project --with numpy --with scipy \
            --with pillow --with nbt python replay_tape.py "$f" --cpu --report
    ) >"$log" 2>&1
    echo $? >"$rcfile"
}

# Launch with a pid-based throttle (job control is off in non-interactive
# bash, so jobs -rp is unreliable). Cap at MAX_CONCURRENT live children.
declare -a WG_PIDS=()
reap_finished() {
    local -a live=()
    local pid
    for pid in ${WG_PIDS[@]+"${WG_PIDS[@]}"}; do
        if kill -0 "$pid" 2>/dev/null; then
            live+=("$pid")
        else
            wait "$pid" 2>/dev/null || true
        fi
    done
    WG_PIDS=(${live[@]+"${live[@]}"})
}
for tape in "${UNIQUE[@]}"; do
    while true; do
        reap_finished
        if [ "${#WG_PIDS[@]}" -lt "$MAX_CONCURRENT" ]; then
            break
        fi
        # Block until any child exits (bash >=4.3); else brief sleep+reap.
        if ! wait -n 2>/dev/null; then
            sleep 0.15
        fi
    done
    run_one "$tape" &
    WG_PIDS+=("$!")
done
# Drain remaining workers (rc files are authoritative, not wait statuses)
for pid in ${WG_PIDS[@]+"${WG_PIDS[@]}"}; do
    wait "$pid" 2>/dev/null || true
done

# --- 5. collect results, fail closed ---------------------------------------
overall=0
declare -a PASS_TAPES=()
declare -a FAIL_TAPES=()

for tape in "${UNIQUE[@]}"; do
    safe=$(safe_name "$tape")
    log="$LOGDIR/${safe}.log"
    rcfile="$LOGDIR/${safe}.rc"
    reason=""
    rc=127
    if [ ! -f "$rcfile" ]; then
        reason="missing worker rc (killed?)"
        overall=1
    else
        rc=$(cat "$rcfile")
        # Signal termination: bash wait stores 128+signal; our subshell may
        # also die on SIGKILL without writing rc (handled above). rc>=128 is
        # treated as killed/signaled.
        if [ "$rc" -ge 128 ] 2>/dev/null; then
            reason="killed/signaled (rc=$rc)"
            overall=1
        elif [ "$rc" -ne 0 ]; then
            reason="gate rc=$rc"
            overall=1
        fi
    fi

    # Report path mirrors replay_tape.py: report/tape_<name>.gate.json
    # name = basename of tape path without .jsonl
    tbase=$(basename "$tape")
    tbase=${tbase%.jsonl}
    # If caller passed a full path, resolve to the real tape basename used by
    # the worker.
    if [ -f "$LOGDIR/${safe}.name" ] && f=$(resolve_tape "$tape" 2>/dev/null); then
        tbase=$(basename "$f" .jsonl)
    fi
    gj="$VERIFY/trace/report/tape_${tbase}.gate.json"

    if [ -z "$reason" ]; then
        if [ ! -f "$gj" ]; then
            reason="missing report $gj"
            overall=1
        else
            # frames_checked must be >0 (harness failure if goldens resolved
            # to nothing). Uses python for portable JSON parse.
            fc=$(python3 -c '
import json,sys
p=sys.argv[1]
try:
    g=json.load(open(p))
except Exception:
    print("ERR"); raise SystemExit(0)
v=g.get("frames_checked", "MISSING")
print(v)
' "$gj" 2>/dev/null || echo ERR)
            if [ "$fc" = "ERR" ] || [ "$fc" = "MISSING" ]; then
                reason="unreadable/missing frames_checked in $gj"
                overall=1
            elif [ "$fc" = "0" ]; then
                reason="zero-checked-frames in $gj"
                overall=1
            fi
        fi
    fi

    if [ -n "$reason" ]; then
        echo "[fast_gate] FAIL  $tape  ($reason)  log=$log"
        FAIL_TAPES+=("$tape")
        # Surface last lines of the log for diagnosis
        if [ -f "$log" ]; then
            tail -n 8 "$log" | sed 's/^/    | /'
        fi
    else
        echo "[fast_gate] PASS  $tape  (rc=0 frames_checked=$fc)"
        PASS_TAPES+=("$tape")
    fi
done

echo "[fast_gate] binary sha256=$BIN_SHA"
echo "[fast_gate] unique tapes: ${UNIQUE[*]}"
echo "[fast_gate] pass set (${#PASS_TAPES[@]}): ${PASS_TAPES[*]:-}"
if [ ${#FAIL_TAPES[@]} -gt 0 ]; then
    echo "[fast_gate] fail set (${#FAIL_TAPES[@]}): ${FAIL_TAPES[*]}"
fi

if [ "$overall" -ne 0 ]; then
    echo "REJECT"
    # Preserve logs on reject for a short window under TMPDIR (not auto-wiped
    # so a human can inspect). Tag with pid.
    echo "[fast_gate] logs kept at $LOGDIR"
    exit 1
fi
rm -rf "$LOGDIR"
echo "ACCEPT"
exit 0
