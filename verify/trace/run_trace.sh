#!/usr/bin/env bash
# run_trace.sh - one entry point for the oracle-vs-magma trace/replay flywheel.
#
#   bash run_trace.sh checkpoints   # pose both games at scripted checkpoints, pixel-diff
#   bash run_trace.sh trajectory    # 200-tick fixed input tape, divergence over time
#   bash run_trace.sh spawns        # observe oracle night mob spawns (ground truth)
#   bash run_trace.sh all
#
# Flags (before the subcommand):
#   --wipe      delete saves/qrl_<seed> and restart the game for CLEAN worldgen
#               (needed if a previous session mutated the world; a stale save is
#               the #1 source of bogus diffs - see mc_capture/capture.sh)
#   --seed N    world seed (default 0)
#
# Oracle startup is handled here: if the qrl bridge (127.0.0.1:25575) is not
# accepting connections, the headless stack (Xvfb :1 + MC via
# java/start_vnc_client.sh) is launched and polled with a REAL socket probe
# (never grep runclient.log - stale lines race the boot). World load takes
# minutes on first generation; every wait has a hard timeout.
set -u

HERE=$(cd "$(dirname "$0")" && pwd)
MAGMA=$(cd "$HERE/../../magma" && pwd)
REPO=$(cd "$MAGMA/../.." && pwd)
JAVA_DIR=${MC_JAVA_DIR:-$REPO/java}
SEED=0
WIPE=0
LAUNCH_LOG=/tmp/mc_launch_trace.out

log()  { echo "[trace] $*"; }
fail() { echo "[trace] FAIL: $*" >&2; tail -n 30 "$JAVA_DIR/runclient.log" 2>/dev/null >&2; exit 1; }

while [ $# -gt 0 ]; do
    case "$1" in
        --wipe) WIPE=1; shift;;
        --seed) SEED="$2"; shift 2;;
        checkpoints|trajectory|spawns|all) CMD="$1"; shift; break;;
        *) echo "usage: run_trace.sh [--wipe] [--seed N] checkpoints|trajectory|spawns|all" >&2; exit 2;;
    esac
done
[ -n "${CMD:-}" ] || { echo "usage: run_trace.sh [--wipe] [--seed N] checkpoints|trajectory|spawns|all" >&2; exit 2; }

probe() {
    python3 -c 'import socket,sys
s=socket.socket(); s.settimeout(1)
try: s.connect(("127.0.0.1",25575)); s.close()
except Exception: sys.exit(1)' 2>/dev/null
}

# --- oracle lifecycle -------------------------------------------------------
if [ "$WIPE" = 1 ]; then
    log "wipe requested: killing any running game (the [G] bracket avoids self-match)"
    pkill -9 -f '[G]radleStart' 2>/dev/null
    pkill -9 -f '[r]unClient'   2>/dev/null
    sleep 3
    SAVE="$JAVA_DIR/Minecraft/run/saves/qrl_${SEED}"
    [ -d "$SAVE" ] && { log "wiping stale save $SAVE"; rm -rf "$SAVE"; }
fi

if ! probe; then
    if [ "$(uname)" = "Darwin" ]; then
        # start_vnc_client.sh is Linux-only (Xvfb); on the Mac the game must
        # already be running (bash java/start_vnc_client.sh (or sunshine/mcwindow)). Just wait for it.
        log "qrl bridge down; start the game with 'bash java/start_vnc_client.sh (or sunshine/mcwindow)', waiting up to 420s..."
    else
        log "qrl bridge down; launching headless stack via start_vnc_client.sh ..."
        ( cd "$JAVA_DIR" && setsid nohup bash start_vnc_client.sh >"$LAUNCH_LOG" 2>&1 </dev/null & )
        log "waiting for the bridge on :25575 (up to 420s)..."
    fi
    up=0
    for _ in $(seq 1 420); do probe && { up=1; break; }; sleep 1; done
    [ "$up" = 1 ] || fail "qrl bridge never accepted a connection within 420s"
fi
log "bridge is accepting connections."

# --- magma build (worktree-local) -----------------------------------------
[ -x "$MAGMA/magma_game" ] || { log "building magma_game ..."; make -C "$MAGMA" game >/dev/null || fail "magma_game build failed"; }

PY="uv run --no-project --with numpy --with pillow --with matplotlib python"

run_checkpoints() { $PY "$HERE/checkpoints.py" --seed "$SEED" || fail "checkpoints run failed"; }
run_trajectory()  { $PY "$HERE/trajectory.py"  --seed "$SEED" || fail "trajectory run failed"; }
run_spawns()      { $PY "$HERE/spawns.py"      --seed "$SEED" || fail "spawns run failed"; }

case "$CMD" in
    checkpoints) run_checkpoints;;
    trajectory)  run_trajectory;;
    spawns)      run_spawns;;
    all)         run_checkpoints; run_trajectory; run_spawns;;
esac
log "done. report: $HERE/report/  raw artifacts: $HERE/out/ (gitignored)"
