#!/usr/bin/env bash
# capture_at_poses.sh - capture REAL Minecraft 1.11.2 frames at an ARBITRARY LIST
# of camera poses supplied in a FILE (as opposed to capture_poses.sh's hardcoded
# POSES array). This is the Java-side engine of the pose-forced FIRST-MINUTE FRAME
# ORACLE (trace/frame_oracle.py): frame_oracle writes one line per checkpoint tick
# with magma's exact player pose, this script teleports the REAL game to each pose
# and grabs the window frame, and frame_oracle pixel-diffs each against the magma
# frame rendered at the SAME pose. Isolating pose lets a diff measure what the two
# RENDERERS draw differently, not where physics drifted them apart.
#
# It keeps capture_poses.sh's hard-won correctness VERBATIM:
#   - kill any running game (the [G] bracket is REQUIRED per env gotchas),
#   - optionally wipe saves/qrl_<SEED> so the world regenerates from CLEAN worldgen,
#   - launch the headless stack ONCE via start_vnc_client.sh (unless a bridge is up),
#   - wait on a REAL TCP socket probe (never grep runclient.log for "listening"),
#   - reset(seed), silence command feedback, force+freeze clear noon, tp per pose,
#   - grab the MC window CONTENT region with ffmpeg x11grab -draw_mouse 0.
#
# Differences from capture_poses.sh, all in service of the first-minute oracle:
#   - poses come from --poses FILE (one per line), not a hardcoded array,
#   - default gamemode is SURVIVAL (draws the hand + hotbar + crosshair + vitals
#     HUD) so the oracle SEES the HUD/hand divergence magma lacks; pass
#     --gamemode spectator for a clean camera hold (no hand/HUD) instead,
#   - a final tp is re-asserted immediately before the grab and the settle loop is
#     short, so in survival the player has no time to fall away from the pose.
#
# POSES FILE format (one pose per line, '#'/blank ignored, whitespace-separated):
#     IDX  FEET_X  FEET_Y  FEET_Z  MC_YAW  MC_PITCH
# FEET_* are world FEET coords (MC `tp` sets feet; the camera eye sits 1.62 above).
# MC_YAW/MC_PITCH are MC convention (yaw 180 faces -Z, POSITIVE pitch looks DOWN) -
# exactly the yaw/pitch columns magma's trace CSV already stores. Output per pose:
#   <OUTDIR>/mc_ck_<IDX>.png   (MC window content region, WxH as grabbed)
#   <OUTDIR>/mc_ck_<IDX>.json  (readback obs pose + capture params)
#
# Anti-rabbit-hole: every wait has a hard timeout; on failure it prints the
# runclient.log tail and exits non-zero rather than looping forever.
set -u

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
ENVDIR="$ROOT/java"
DEFOUT="$ROOT/magma/trace/out/frame_oracle"
export JAVA_HOME=/usr/lib/jvm/java-8-openjdk-amd64
export PATH="$JAVA_HOME/bin:$PATH"

SEED=0
QRL_HOST=127.0.0.1
QRL_PORT=25575
MC_DISPLAY=:1
FRESH=0
EYE_HEIGHT=1.62
FOV=70
GAMEMODE=survival        # survival draws hand+HUD (the point); spectator = clean hold
SETTLE=20                # short tp iterations so chunks build before the grab
POSES_FILE=""
OUTDIR="$DEFOUT"
WIPE=1                   # wipe the seed save for clean worldgen (0 to reuse)
NOLAUNCH=0               # 1 = assume a bridge is already up, do not launch/kill
LAUNCH_LOG=/tmp/mc_launch.out
RUNCLIENT_LOG="$ENVDIR/runclient.log"

usage() {
    cat >&2 <<EOF
usage: capture_at_poses.sh --poses FILE [--out DIR] [--seed N] [--fov F]
                           [--gamemode survival|spectator] [--settle N]
                           [--eye-height H] [--host HOST] [--port PORT]
                           [--display :N] [--fresh] [--no-wipe] [--no-launch]
  --poses FILE     required; lines "IDX FEET_X FEET_Y FEET_Z MC_YAW MC_PITCH"
  --out DIR        output dir (default $DEFOUT)
  --gamemode       survival (default; hand+HUD) | spectator (clean camera)
  --settle N       short tp iterations before grabbing (default $SETTLE)
  --host HOST      QRL bridge host (default $QRL_HOST)
  --port PORT      QRL bridge port (default $QRL_PORT)
  --display :N     X display containing the game (default $MC_DISPLAY)
  --fresh          delete/recreate qrl_<seed> during bridge reset
  --no-wipe        do NOT delete saves/qrl_<seed> before reset
  --no-launch      assume the qrl bridge is already up (skip kill+launch+wipe)
EOF
}

while [ $# -gt 0 ]; do
    case "$1" in
        --poses)      POSES_FILE="$2"; shift 2;;
        --out)        OUTDIR="$2"; shift 2;;
        --seed)       SEED="$2"; shift 2;;
        --fov)        FOV="$2"; shift 2;;
        --gamemode)   GAMEMODE="$2"; shift 2;;
        --settle)     SETTLE="$2"; shift 2;;
        --eye-height) EYE_HEIGHT="$2"; shift 2;;
        --host)        QRL_HOST="$2"; shift 2;;
        --port)        QRL_PORT="$2"; shift 2;;
        --display)     MC_DISPLAY="$2"; shift 2;;
        --fresh)       FRESH=1; shift;;
        --no-wipe)    WIPE=0; shift;;
        --no-launch)  NOLAUNCH=1; shift;;
        -h|--help)    usage; exit 0;;
        *) echo "unknown arg: $1" >&2; usage; exit 2;;
    esac
done
[ -n "$POSES_FILE" ] || { echo "FAIL: --poses is required" >&2; usage; exit 2; }
[ -s "$POSES_FILE" ] || { echo "FAIL: poses file $POSES_FILE missing/empty" >&2; exit 2; }
POSES_FILE="$(realpath "$POSES_FILE")"
[[ "$QRL_PORT" =~ ^[0-9]+$ ]] && [ "$QRL_PORT" -ge 1 ] && [ "$QRL_PORT" -le 65535 ] ||
    { echo "FAIL: --port must be an integer from 1 to 65535" >&2; exit 2; }
[[ "$MC_DISPLAY" =~ ^:[0-9]+$ ]] ||
    { echo "FAIL: --display must have the form :N" >&2; exit 2; }
export DISPLAY="$MC_DISPLAY"
TMP_PREFIX="/tmp/oracle_${QRL_PORT}"
mkdir -p "$OUTDIR"
OUTDIR="$(realpath "$OUTDIR")"

log()  { echo "[cap-poses] $*"; }
fail() { echo "[cap-poses] FAIL: $*" >&2; echo "----- runclient.log tail -----" >&2; tail -n 40 "$RUNCLIENT_LOG" 2>/dev/null >&2; exit 1; }

probe_bridge() {
    python3 - "$QRL_HOST" "$QRL_PORT" <<'PY' 2>/dev/null
import socket, sys
s = socket.socket()
s.settimeout(1)
try:
    s.connect((sys.argv[1], int(sys.argv[2])))
    s.close()
except Exception:
    sys.exit(1)
PY
}

if [ "$NOLAUNCH" = 0 ]; then
    [ "$QRL_HOST" = 127.0.0.1 ] && [ "$QRL_PORT" = 25575 ] && [ "$MC_DISPLAY" = :1 ] ||
        fail "automatic launch only supports 127.0.0.1:25575 on display :1; use --no-launch"
    log "killing any running game..."
    pkill -9 -f '[G]radleStart' 2>/dev/null
    pkill -9 -f '[r]unClient'   2>/dev/null
    sleep 3
    if [ "$WIPE" = 1 ]; then
        SAVE="$ENVDIR/Minecraft/run/saves/qrl_${SEED}"
        [ -d "$SAVE" ] && { log "wiping stale save $SAVE"; rm -rf "$SAVE"; }
    fi
    log "launching headless game via start_vnc_client.sh ..."
    ( cd "$ENVDIR" && setsid nohup bash start_vnc_client.sh >"$LAUNCH_LOG" 2>&1 </dev/null & )
fi

log "waiting for qrl bridge at $QRL_HOST:$QRL_PORT on display $MC_DISPLAY (up to 420s)..."
listened=0
for _ in $(seq 1 420); do
    if probe_bridge; then listened=1; break; fi
    sleep 1
done
[ "$listened" = 1 ] || fail "qrl bridge never accepted a connection within 420s"
log "bridge is accepting connections."

# --- reset seed ONCE + freeze scene; then teleport+grab per pose ---------------
# One persistent python round-trip: reset, set gamerules + gamemode, then loop over
# the poses file. Per pose it re-asserts tp for SETTLE ticks (chunks build), does a
# FINAL tp, then signals the shell to grab and blocks until the grab is done.
cd "$ENVDIR"
rm -f "${TMP_PREFIX}"_grab_ready_* "${TMP_PREFIX}"_grab_done_* "${TMP_PREFIX}"_all_done
python3 - "$SEED" "$EYE_HEIGHT" "$GAMEMODE" "$SETTLE" "$POSES_FILE" \
    "$QRL_HOST" "$QRL_PORT" "$FRESH" "$TMP_PREFIX" <<'PY' &
import sys, json, time, os
import qrl_client
seed  = int(sys.argv[1]); eyeh = float(sys.argv[2])
gm    = sys.argv[3];      settle = int(sys.argv[4])
poses_file = sys.argv[5]
host = sys.argv[6]; port = int(sys.argv[7])
fresh = bool(int(sys.argv[8])); tmp_prefix = sys.argv[9]

poses = []
with open(poses_file) as f:
    for line in f:
        s = line.strip()
        if not s or s.startswith("#"):
            continue
        v = s.split()
        if len(v) < 6:
            print("[py] skip bad pose line:", s, file=sys.stderr); continue
        poses.append((int(v[0]), float(v[1]), float(v[2]), float(v[3]),
                      float(v[4]), float(v[5])))

e = qrl_client.NetheriteEnv(host=host, port=port)
o = e.reset({"seed": seed, "mode": "survival", "type": "default", "fresh": fresh})
if not o.get("ok"):
    print("reset not ok:", o, file=sys.stderr); sys.exit(1)
print("[py] spawn ~ (%.1f,%.1f,%.1f)" % (o["x"], o["y"], o["z"]), file=sys.stderr)
setup = [
    "gamerule sendCommandFeedback false",
    "gamerule logAdminCommands false",
    "gamerule doDaylightCycle false",
    "gamerule doWeatherCycle false",
    "gamerule doMobSpawning false",
    "time set 6000",
    "weather clear 1000000",
    "gamemode %s @a" % gm,
]
print("[py] setup:", e._cmd({"cmd": "runcmds", "action": {"cmds": setup}}), file=sys.stderr)

for (idx, fx, fy, fz, myaw, mpitch) in poses:
    tp = "tp @a %g %g %g %g %g" % (fx, fy, fz, myaw, mpitch)
    # Build surrounding chunks while pinning the pose. Do not use qrl step here:
    # immediately after a fresh integrated-server reload its tick barrier can
    # retain a stale waiter and block forever even though ordinary commands and
    # rendering are healthy. Pixel capture needs a settled scene, not N exact
    # simulation ticks.
    for _ in range(settle):
        e._cmd({"cmd": "runcmds", "action": {"cmds": [tp]}})
        time.sleep(0.05)
    # final re-assert immediately before the grab so survival gravity cannot drift
    # the player away from the pose; a short settle lets the frame render.
    e._cmd({"cmd": "runcmds", "action": {"cmds": [tp]}})
    time.sleep(0.5)
    e._cmd({"cmd": "runcmds", "action": {"cmds": [tp]}})
    time.sleep(0.3)
    obs = e.obs()
    with open("%s_pose_%d.json" % (tmp_prefix, idx), "w") as f:
        json.dump({"seed": seed, "checkpoint": idx,
                   "x": obs["x"], "y": obs["y"], "z": obs["z"],
                   "yaw": obs["yaw"], "pitch": obs["pitch"],
                   "mc_yaw": myaw, "mc_pitch": mpitch,
                   "health": obs["health"], "on_ground": obs["on_ground"],
                   "gamemode": gm}, f)
    print("[py] ck %d final obs:" % idx, json.dumps(obs)[:160], file=sys.stderr)
    open("%s_grab_ready_%d" % (tmp_prefix, idx), "w").close()
    for _ in range(600):
        if os.path.exists("%s_grab_done_%d" % (tmp_prefix, idx)):
            break
        time.sleep(0.1)
open(tmp_prefix + "_all_done", "w").close()
e.close()
PY
QRL_PID=$!

# --- window geometry helper (content region only, no title bar) ---
grab_pose() {
    local idx="$1" outpng="$2"
    local GEOM WH ABS W H AX AY
    GEOM=$(DISPLAY="$MC_DISPLAY" xwininfo -root -tree 2>/dev/null | grep -i "Minecraft 1.11.2" | head -1)
    WH=$(echo "$GEOM"  | grep -oE '[0-9]+x[0-9]+\+[0-9]+\+[0-9]+' | head -1)
    ABS=$(echo "$GEOM" | grep -oE '\+[0-9]+\+[0-9]+$' | head -1)
    W=$(echo "$WH" | cut -dx -f1)
    H=$(echo "$WH" | sed -E 's/^[0-9]+x([0-9]+).*/\1/')
    AX=$(echo "$ABS" | cut -d+ -f2)
    AY=$(echo "$ABS" | cut -d+ -f3)
    if [ -z "${W:-}" ] || [ -z "${H:-}" ] || [ -z "${AX:-}" ] || [ -z "${AY:-}" ]; then
        log "ck $idx: window geometry not found; full-display 1280x720 grab"
        W=1280; H=720; AX=0; AY=0
    fi
    log "ck $idx: MC window content ${W}x${H} at (${AX},${AY}); grabbing -> $outpng"
    DISPLAY="$MC_DISPLAY" ffmpeg -hide_banner -loglevel error -y \
        -f x11grab -draw_mouse 0 -video_size "${W}x${H}" \
        -i "${MC_DISPLAY}.0+${AX},${AY}" \
        -frames:v 1 "$outpng" || fail "ffmpeg x11grab failed (ck $idx)"
    [ -s "$outpng" ] || fail "$outpng empty (ck $idx)"
    echo "$W $H"
}

# --- per-pose: wait for ready, grab window, merge json ---
IDXS=$(awk 'NF && $1 !~ /^#/ {print $1}' "$POSES_FILE")
for idx in $IDXS; do
    log "waiting for ck $idx to settle (up to 120s)..."
    ready=0
    for _ in $(seq 1 1200); do
        [ -f "${TMP_PREFIX}_grab_ready_$idx" ] && { ready=1; break; }
        kill -0 "$QRL_PID" 2>/dev/null || fail "qrl python exited before ck $idx"
        sleep 0.1
    done
    [ "$ready" = 1 ] || fail "ck $idx never became ready within 120s"

    DIMS=$(grab_pose "$idx" "$OUTDIR/mc_ck_$idx.png")
    GW=$(echo "$DIMS" | tail -1 | cut -d' ' -f1)
    GH=$(echo "$DIMS" | tail -1 | cut -d' ' -f2)

    python3 - "$OUTDIR/mc_ck_$idx.json" "${TMP_PREFIX}_pose_$idx.json" "$GW" "$GH" "$FOV" <<'PY'
import sys, json
out, src, w, h, fov = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4]), int(sys.argv[5])
p = json.load(open(src))
p.update({"fov": fov, "width": w, "height": h,
          "notes": ("REAL MC 1.11.2 frame, llvmpipe SW GL, seed 0 default world, "
                    "first-minute frame oracle. Teleported to magma's checkpoint "
                    "pose (mc_yaw/mc_pitch); matching magma pose is "
                    "magma_yaw=180-mc_yaw, magma_pitch=-mc_pitch, eye 1.62 above "
                    "feet. Window CONTENT region only.")})
json.dump(p, open(out, "w"), indent=2)
print("[cap-poses] wrote", out)
PY
    touch "${TMP_PREFIX}_grab_done_$idx"
    log "ck $idx captured."
done

for _ in $(seq 1 300); do [ -f "${TMP_PREFIX}_all_done" ] && break; sleep 1; done
wait "$QRL_PID" 2>/dev/null

if [ "$NOLAUNCH" = 0 ]; then
    log "stopping game..."
    pkill -9 -f '[G]radleStart' 2>/dev/null
    pkill -9 -f '[r]unClient'   2>/dev/null
fi
log "done. captured -> $OUTDIR/mc_ck_<idx>.png + mc_ck_<idx>.json"
