#!/usr/bin/env bash
# capture_poses.sh - capture MULTIPLE REAL Minecraft 1.11.2 frames (seed 0), one
# per camera pose in the POSES list below, to serve as the game-verify goldens
# that magma's arbitrary-pose seed->raster path (game_candidate.c) is diffed
# against. This generalizes capture.sh (which captures the ONE frozen rung-4
# pose) to a list, KEEPING its hard-won correctness verbatim:
#   - kill any running game (the [G] bracket is REQUIRED per env gotchas),
#   - wipe saves/qrl_<SEED> so the world regenerates from CLEAN worldgen,
#   - launch the headless stack ONCE via start_vnc_client.sh,
#   - wait on a REAL TCP socket probe (never grep runclient.log for "listening",
#     a stale line races ahead of the new boot),
#   - reset(seed), silence command feedback, force+freeze clear noon,
#     gamemode spectator (aerial hold, no hand/hotbar), tp to each pose,
#     tick chunks in, NO fluid() flood (magma's world is clean),
#   - grab the MC window CONTENT region with ffmpeg x11grab -draw_mouse 0.
#
# The game launches once; all poses are captured in the same session (reset seed
# is identical, only the tp changes), so worldgen is shared and cheap.
#
# Outputs (this dir): mc_pose_<i>.png + pose_<i>.json for each pose i (0-based).
# pose_0 is the frozen rung-4 pose, so mc_pose_0.png == the rung-4 golden.
#
# Fresh checkout note: start_vnc_client.sh runs gradle --offline; on a brand-new
# checkout the deps are unresolved and the launch fails. Export MC_GRADLE_ONLINE=1
# once to force a real resolve (per AGENTS.md / docs/RUNBOOK.md), then rerun.
#
# Anti-rabbit-hole: every wait has a hard timeout; on failure it prints the
# runclient.log tail and exits non-zero rather than looping forever.
set -u

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
ENVDIR="$ROOT/java"
OUTDIR="$ROOT/verify/mc_capture"
export JAVA_HOME=/usr/lib/jvm/java-8-openjdk-amd64
export PATH="$JAVA_HOME/bin:$PATH"
export DISPLAY=:1

SEED=0
EYE_HEIGHT=1.62   # MC player eye height; tp sets FEET, so feet = eye - eyeh
FOV=70            # MC "Normal" FOV default == magma fov_deg
LAUNCH_LOG=/tmp/mc_launch.out
RUNCLIENT_LOG="$ENVDIR/runclient.log"

# --- POSE LIST -------------------------------------------------------------
# One entry per pose: "EYE_X EYE_Y EYE_Z MC_YAW MC_PITCH". MC convention: yaw 180
# faces -Z, POSITIVE pitch looks DOWN. The MATCHING magma pose (used by
# run_game_verify.sh) is magma_yaw = 180 - MC_yaw, magma_pitch = -MC_pitch.
# These are chosen over seed-0 terrain near origin at varied yaw/pitch/eye height:
#   0: frozen rung-4 aerial pose (MC yaw 180 / pitch 35 == magma yaw 0 / -35)
#   1: ground-level eye looking level toward -Z
#   2: mid-height looking toward +X (MC yaw 90 == magma yaw 90)
#   3: higher aerial, steeper downward tilt
POSES=(
  "8.2994 95.0 40.0 180 35"
  "8.0    82.0 24.0 180 10"
  "0.0    88.0  8.0  90 20"
  "8.0   112.0 48.0 180 55"
)

log() { echo "[capture] $*"; }
fail() { echo "[capture] FAIL: $*" >&2; echo "----- runclient.log tail -----" >&2; tail -n 40 "$RUNCLIENT_LOG" 2>/dev/null >&2; exit 1; }

mkdir -p "$OUTDIR"

# --- 1. kill any old game (the [G] bracket is REQUIRED per env gotchas) ---
log "killing any running game..."
pkill -9 -f '[G]radleStart' 2>/dev/null
pkill -9 -f '[r]unClient'   2>/dev/null
sleep 3

# --- 1b. wipe the seed's saved world so it regenerates from CLEAN worldgen ---
SAVE="$ENVDIR/Minecraft/run/saves/qrl_${SEED}"
[ -d "$SAVE" ] && { log "wiping stale save $SAVE"; rm -rf "$SAVE"; }

# --- 2. launch fresh headless stack + game ONCE ---
log "launching headless game via start_vnc_client.sh ..."
( cd "$ENVDIR" && setsid nohup bash start_vnc_client.sh >"$LAUNCH_LOG" 2>&1 </dev/null & )

# --- 3a. wait for the qrl bridge to actually ACCEPT a TCP connection on :25575 ---
log "waiting for qrl bridge to accept connections on :25575 (up to 360s)..."
listened=0
for i in $(seq 1 360); do
    if python3 -c 'import socket,sys; s=socket.socket(); s.settimeout(1)
try:
    s.connect(("127.0.0.1",25575)); s.close()
except Exception:
    sys.exit(1)' 2>/dev/null; then listened=1; break; fi
    sleep 1
done
[ "$listened" = 1 ] || fail "qrl bridge never accepted a connection within 360s"
log "bridge is accepting connections."

# --- 3b. reset seed 0 ONCE + freeze scene; then teleport+grab per pose --------
# All qrl interaction runs in ONE persistent python round-trip: reset, then loop
# over poses (tp + tick + write /tmp/qrl_pose_<i>.json). It signals us per pose
# via a ready file so we can grab the window between teleports.
cd "$ENVDIR"
rm -f /tmp/qrl_pose_*.json /tmp/qrl_grab_ready /tmp/qrl_grab_done /tmp/qrl_all_done
POSES_STR="${POSES[*]}"
log "reset(seed=$SEED) + spectator + frozen noon; capturing ${#POSES[@]} poses ..."
python3 - "$SEED" "$EYE_HEIGHT" "$POSES_STR" <<'PY' &
import sys, json, time, os
import qrl_client
seed = int(sys.argv[1]); eyeh = float(sys.argv[2])
poses = sys.argv[3].split()
n = len(poses) // 5
e = qrl_client.NetheriteEnv()
o = e.reset({"seed": seed, "mode": "survival", "type": "default"})
if not o.get("ok"):
    print("reset not ok:", o, file=sys.stderr); sys.exit(1)
print("[py] spawn ~ (%.1f,%.1f,%.1f)" % (o["x"], o["y"], o["z"]), file=sys.stderr)
setup = [
    "gamerule sendCommandFeedback false",
    "gamerule logAdminCommands false",
    "gamerule doDaylightCycle false",
    "gamerule doWeatherCycle false",
    "time set 6000",
    "weather clear 1000000",
    "gamemode spectator @a",
]
print("[py] setup:", e._cmd({"cmd": "runcmds", "action": {"cmds": setup}}), file=sys.stderr)
for i in range(n):
    px = float(poses[5*i+0]); eye_y = float(poses[5*i+1]); pz = float(poses[5*i+2])
    yaw = float(poses[5*i+3]); pitch = float(poses[5*i+4])
    feet_y = eye_y - eyeh
    for _ in range(80):
        e.step({})
        e._cmd({"cmd": "runcmds", "action": {"cmds":
                ["tp @a %g %g %g %g %g" % (px, feet_y, pz, yaw, pitch)]}})
        time.sleep(0.05)
    time.sleep(2.5)  # let teleport toast / chat fade before the grab
    obs = e.obs()
    with open("/tmp/qrl_pose_%d.json" % i, "w") as f:
        json.dump({"seed": seed, "x": obs["x"], "y": obs["y"], "z": obs["z"],
                   "yaw": obs["yaw"], "pitch": obs["pitch"],
                   "mc_yaw": yaw, "mc_pitch": pitch,
                   "health": obs["health"], "on_ground": obs["on_ground"]}, f)
    print("[py] pose %d final obs:" % i, json.dumps(obs)[:160], file=sys.stderr)
    # signal the shell to grab, wait for it to finish before the next tp
    open("/tmp/qrl_grab_ready_%d" % i, "w").close()
    for _ in range(600):
        if os.path.exists("/tmp/qrl_grab_done_%d" % i):
            break
        time.sleep(0.1)
open("/tmp/qrl_all_done", "w").close()
e.close()
PY
QRL_PID=$!

# --- window geometry helper (content region only, no title bar) ---
grab_pose() {
    local idx="$1" outpng="$2"
    local GEOM WH ABS W H AX AY
    GEOM=$(DISPLAY=:1 xwininfo -root -tree 2>/dev/null | grep -i "Minecraft 1.11.2" | head -1)
    WH=$(echo "$GEOM"  | grep -oE '[0-9]+x[0-9]+\+[0-9]+\+[0-9]+' | head -1)
    ABS=$(echo "$GEOM" | grep -oE '\+[0-9]+\+[0-9]+$' | head -1)
    W=$(echo "$WH" | cut -dx -f1)
    H=$(echo "$WH" | sed -E 's/^[0-9]+x([0-9]+).*/\1/')
    AX=$(echo "$ABS" | cut -d+ -f2)
    AY=$(echo "$ABS" | cut -d+ -f3)
    if [ -z "${W:-}" ] || [ -z "${H:-}" ] || [ -z "${AX:-}" ] || [ -z "${AY:-}" ]; then
        log "pose $idx: window geometry not found; full-display 1280x720 grab"
        W=1280; H=720; AX=0; AY=0
    fi
    log "pose $idx: MC window content ${W}x${H} at (${AX},${AY}); grabbing -> $outpng"
    DISPLAY=:1 ffmpeg -hide_banner -loglevel error -y \
        -f x11grab -draw_mouse 0 -video_size "${W}x${H}" -i ":1.0+${AX},${AY}" \
        -frames:v 1 "$outpng" || fail "ffmpeg x11grab failed (pose $idx)"
    [ -s "$outpng" ] || fail "$outpng empty (pose $idx)"
    echo "$W $H"   # return grabbed dims
}

# --- 4. per-pose: wait for ready, grab window, write pose_<i>.json ---
NP=${#POSES[@]}
for ((i=0; i<NP; i++)); do
    log "waiting for pose $i to settle (up to 120s)..."
    ready=0
    for _ in $(seq 1 1200); do
        [ -f "/tmp/qrl_grab_ready_$i" ] && { ready=1; break; }
        # bail if the python side died
        kill -0 "$QRL_PID" 2>/dev/null || { fail "qrl python exited before pose $i"; }
        sleep 0.1
    done
    [ "$ready" = 1 ] || fail "pose $i never became ready within 120s"

    DIMS=$(grab_pose "$i" "$OUTDIR/mc_pose_$i.png")
    GW=$(echo "$DIMS" | tail -1 | cut -d' ' -f1)
    GH=$(echo "$DIMS" | tail -1 | cut -d' ' -f2)

    # merge readback pose + capture params -> pose_<i>.json
    python3 - "$OUTDIR/pose_$i.json" "/tmp/qrl_pose_$i.json" "$GW" "$GH" "$FOV" "$i" <<'PY'
import sys, json
out, src, w, h, fov, idx = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4]), int(sys.argv[5]), int(sys.argv[6])
p = json.load(open(src))
p.update({"fov": fov, "width": w, "height": h, "pose_index": idx,
          "notes": ("REAL MC 1.11.2 client frame, llvmpipe software GL, seed 0 "
                    "default world. Spectator gamemode (aerial hold, no hand/hotbar). "
                    "Frozen clear noon (time 6000, weather clear, cycles off), command "
                    "feedback silenced, no water flood. Window CONTENT region only. "
                    "mc_yaw/mc_pitch are the teleport heading; the matching magma pose "
                    "is magma_yaw=180-mc_yaw, magma_pitch=-mc_pitch. COARSE golden.")})
json.dump(p, open(out, "w"), indent=2)
print("[capture] wrote", out)
PY
    # tell the python side to advance to the next pose
    touch "/tmp/qrl_grab_done_$i"
    log "pose $i captured."
done

# --- 5. wait for python to finish, stop the game ---
for _ in $(seq 1 300); do [ -f /tmp/qrl_all_done ] && break; sleep 1; done
wait "$QRL_PID" 2>/dev/null

log "stopping game..."
pkill -9 -f '[G]radleStart' 2>/dev/null
pkill -9 -f '[r]unClient'   2>/dev/null

# keep pose_0 as the canonical rung-4 golden name too, for continuity
if [ -f "$OUTDIR/mc_pose_0.png" ]; then
    cp -f "$OUTDIR/mc_pose_0.png" "$OUTDIR/mc_frame.png"
    cp -f "$OUTDIR/pose_0.json"   "$OUTDIR/pose.json"
    log "pose 0 mirrored to mc_frame.png / pose.json (rung-4 continuity)"
fi
log "done. captured $NP poses -> $OUTDIR/mc_pose_<i>.png + pose_<i>.json"
