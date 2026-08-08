#!/usr/bin/env bash
# capture.sh - capture ONE frame of the REAL Minecraft 1.11.2 client (seed 0,
# fixed camera pose) to serve as the rung-4 whole-frame golden that magma's
# full seed->raster path is diffed against.
#
# Idempotent: kills any running game, relaunches the headless VNC/Xvfb stack +
# game via start_vnc_client.sh, waits for the qrl bridge + a loaded world,
# resets to SEED 0, teleports the player to a FIXED pose, forces chunk render
# with fluid()+steps, grabs the MC window region with ffmpeg x11grab, writes
# mc_frame.png + pose.json, then stops the game.
#
# Anti-rabbit-hole: every wait has a hard timeout; on failure it prints the
# relevant log tail and exits non-zero rather than looping forever.
set -u

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
ENVDIR="$ROOT/java"
OUTDIR="$ROOT/verify/mc_capture"
export JAVA_HOME=/usr/lib/jvm/java-8-openjdk-amd64
export PATH="$JAVA_HOME/bin:$PATH"
export DISPLAY=:1

# --- FIXED aerial pose, MATCHED to the rung-3/rung-4 magma ChunkScene camera ---
# magma renders the seed-0 3x3-chunk-around-origin scene from world eye
# (POS_X, POS_EYE_Y, POS_Z) with magma yaw 0 / pitch -35deg (toward -Z, tilted
# down). See rung4_candidate.c: it PRINTS this exact resolved pose. The MC camera
# convention that reproduces magma forward is yaw 180 / pitch +35 (MC: yaw 180
# faces -Z, POSITIVE pitch looks down). Keep these in sync with rung4_candidate's
# POSE line (they are deterministic for seed 0 + the current mesher).
SEED=0
POS_X=8.3         # ChunkScene solid-centroid X
POS_EYE_Y=95.0    # ChunkScene cam eye Y (terrain top ~79 + 16)
POS_Z=40.0        # ChunkScene cam Z (south of the terrain, looking -Z)
EYE_HEIGHT=1.62   # MC player eye height; tp sets FEET, so feet = eye - 1.62
YAW=180           # MC heading for magma forward -Z
PITCH=35          # MC pitch (down) for magma pitch -35
FOV=70            # MC "Normal" FOV (gameSettings default) == ChunkScene fov_deg
LAUNCH_LOG=/tmp/mc_launch.out
RUNCLIENT_LOG="$ENVDIR/runclient.log"

log() { echo "[capture] $*"; }
fail() { echo "[capture] FAIL: $*" >&2; echo "----- runclient.log tail -----" >&2; tail -n 40 "$RUNCLIENT_LOG" 2>/dev/null >&2; exit 1; }

mkdir -p "$OUTDIR"

# --- 1. kill any old game (the [G] bracket is REQUIRED per env gotchas) ---
log "killing any running game..."
pkill -9 -f '[G]radleStart' 2>/dev/null
pkill -9 -f '[r]unClient'   2>/dev/null
sleep 3

# --- 1b. wipe the seed's saved world so it regenerates from CLEAN worldgen ---
# (reset() reuses an existing save; a stale save can carry contamination from a
# previous run, e.g. flooded water. Delete it for a reproducible golden.)
SAVE="$ENVDIR/Minecraft/run/saves/qrl_${SEED}"
[ -d "$SAVE" ] && { log "wiping stale save $SAVE"; rm -rf "$SAVE"; }

# --- 2. launch fresh headless stack + game (start_vnc_client.sh backgrounds gradlew) ---
log "launching headless game via start_vnc_client.sh ..."
( cd "$ENVDIR" && setsid nohup bash start_vnc_client.sh >"$LAUNCH_LOG" 2>&1 </dev/null & )

# --- 3a. wait for the qrl bridge to actually ACCEPT a TCP connection on :25575 ---
# NB: do NOT grep runclient.log for "listening" - a stale line from a previous
# game survives the relaunch and matches instantly, racing ahead of the new boot
# (ConnectionRefused). Probe the real socket instead (old port is closed by the
# pkill above, so a successful connect proves the NEW bridge is up).
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

# --- 3b/4/5. reset seed 0, teleport to fixed pose, force render (fluid + steps) ---
# All qrl interaction happens in one python round-trip; it writes the ACTUAL
# resulting pose (read back from obs) to /tmp/qrl_pose.json.
log "reset(seed=$SEED) + spectator + teleport to fixed AERIAL pose + tick chunks ..."
cd "$ENVDIR"
python3 - "$SEED" "$YAW" "$PITCH" "$POS_X" "$POS_EYE_Y" "$POS_Z" "$EYE_HEIGHT" \
    <<'PY' || fail "qrl interaction failed (see stderr)"
import sys, json, time
import qrl_client
seed  = int(sys.argv[1])
yaw   = float(sys.argv[2]); pitch = float(sys.argv[3])
px    = float(sys.argv[4]); eye_y = float(sys.argv[5]); pz = float(sys.argv[6])
eyeh  = float(sys.argv[7])
feet_y = eye_y - eyeh   # tp sets FEET; we want the camera EYE at eye_y
e = qrl_client.NetheriteEnv()
# reset auto-launches the world if none is loaded; polls until ready (<=120s).
o = e.reset({"seed": seed, "mode": "survival", "type": "default"})
if not o.get("ok"):
    print("reset not ok:", o, file=sys.stderr); sys.exit(1)
print("[py] spawn ~ (%.1f,%.1f,%.1f)" % (o["x"], o["y"], o["z"]), file=sys.stderr)
# Clean, deterministic scene: silence command feedback, force+freeze clear noon.
# SPECTATOR so the aerial camera holds altitude (no gravity/collision, and no
# hand/hotbar drawn - closer to magma which draws neither). Absolute tp to the
# ChunkScene eye. Do NOT flood water (magma's world is clean).
setup = [
    "gamerule sendCommandFeedback false",
    "gamerule logAdminCommands false",
    "gamerule doDaylightCycle false",
    "gamerule doWeatherCycle false",
    "gamerule doMobSpawning false",
    "gamerule doFireTick false",
    "gamerule randomTickSpeed 0",
    "time set 6000",       # noon
    "weather clear 1000000",
    "gamemode spectator @a",
    "tp @a %g %g %g %g %g" % (px, feet_y, pz, yaw, pitch),
]
print("[py] runcmds:", e._cmd({"cmd": "runcmds", "action": {"cmds": setup}}), file=sys.stderr)
# Advance client ticks so surrounding chunks build + render, re-asserting the tp
# each tick so any residual motion cannot drift the aerial camera.
for _ in range(80):
    e.step({})
    e._cmd({"cmd": "runcmds", "action": {"cmds":
            ["tp @a %g %g %g %g %g" % (px, feet_y, pz, yaw, pitch)]}})
    time.sleep(0.05)
time.sleep(2.5)  # let any teleport toast / chat fade before the grab
obs = e.obs()
with open("/tmp/qrl_pose.json", "w") as f:
    json.dump({"seed": seed, "x": obs["x"], "y": obs["y"], "z": obs["z"],
               "yaw": obs["yaw"], "pitch": obs["pitch"],
               "health": obs["health"], "on_ground": obs["on_ground"]}, f)
print("[py] final obs:", json.dumps(obs)[:200], file=sys.stderr)
e.close()
PY
[ -f /tmp/qrl_pose.json ] || fail "qrl pose file not written"

# --- 6. locate the MC window and grab exactly its content region ---
log "locating Minecraft window geometry..."
GEOM=$(DISPLAY=:1 xwininfo -root -tree 2>/dev/null | grep -i "Minecraft 1.11.2" | head -1)
# xwininfo tree line: ... "Minecraft 1.11.2": (...)  WxH+relX+relY  +absX+absY
WH=$(echo "$GEOM"  | grep -oE '[0-9]+x[0-9]+\+[0-9]+\+[0-9]+' | head -1)
ABS=$(echo "$GEOM" | grep -oE '\+[0-9]+\+[0-9]+$' | head -1)
W=$(echo "$WH" | cut -dx -f1)
H=$(echo "$WH" | sed -E 's/^[0-9]+x([0-9]+).*/\1/')
AX=$(echo "$ABS" | cut -d+ -f2)
AY=$(echo "$ABS" | cut -d+ -f3)
if [ -z "${W:-}" ] || [ -z "${H:-}" ] || [ -z "${AX:-}" ] || [ -z "${AY:-}" ]; then
    log "window geometry not found; falling back to full-display 1280x720 grab"
    W=1280; H=720; AX=0; AY=0
fi
log "MC window content ${W}x${H} at (${AX},${AY})"

log "grabbing frame with ffmpeg x11grab ..."
DISPLAY=:1 ffmpeg -hide_banner -loglevel error -y \
    -f x11grab -draw_mouse 0 -video_size "${W}x${H}" -i ":1.0+${AX},${AY}" \
    -frames:v 1 "$OUTDIR/mc_frame.png" || fail "ffmpeg x11grab failed"
[ -s "$OUTDIR/mc_frame.png" ] || fail "mc_frame.png empty"

# --- 6b. instrumented camera dump (self-describing golden) ---
# Requires qrl cmd "camera". Writes camera.json + options snapshot beside the PNG.
log "dumping camera.json (JVM eye/FOV/options/world) ..."
cd "$ENVDIR"
python3 - "$OUTDIR" <<'PY' || log "WARN: camera dump failed (rebuild mod if unknown cmd)"
import sys, json, shutil
from pathlib import Path
import qrl_client
outdir = Path(sys.argv[1])
e = qrl_client.NetheriteEnv()
cam = e.camera(file=str(outdir / "camera.json"))
print("[capture] camera:", json.dumps({k: cam.get(k) for k in
    ("ok","eye_x","eye_y","eye_z","yaw","pitch","fov_setting","fov_effective",
     "fov_modifier","is_flying","display_w","display_h") if k in cam or True}, indent=2)[:800])
if cam.get("ok"):
    (outdir / "camera.json").write_text(json.dumps(cam, indent=2))
opt = Path("Minecraft/run/options.txt")
if opt.is_file():
    shutil.copy2(opt, outdir / "options_snapshot.txt")
e.close()
PY

# --- 7. write pose.json (merge actual pose + camera provenance) ---
python3 - "$OUTDIR/pose.json" "$W" "$H" "$FOV" "$OUTDIR" <<'PY'
import sys, json
from pathlib import Path
out, w, h, fov, outdir = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4]), sys.argv[5]
p = json.load(open("/tmp/qrl_pose.json"))
cam_path = Path(outdir) / "camera.json"
cam = json.loads(cam_path.read_text()) if cam_path.is_file() else {}
p.update({
    "fov": fov, "width": w, "height": h,
    "eye_x": cam.get("eye_x"), "eye_y": cam.get("eye_y"), "eye_z": cam.get("eye_z"),
    "fov_setting": cam.get("fov_setting"),
    "fov_effective": cam.get("fov_effective"),
    "fov_modifier": cam.get("fov_modifier"),
    "magma_yaw_deg": cam.get("magma_yaw_deg"),
    "magma_pitch_deg": cam.get("magma_pitch_deg"),
    "camera_json": str(cam_path) if cam_path.is_file() else None,
    "notes": ("Instrumented capture: camera.json is source of truth for eye/FOV. "
              "Spectator isFlying multiplies FOV by ~1.1 (use fov_effective, not 70). "
              "Feet y in pose; eye_y = feet + eye_height. Fast profile pins in options_snapshot.txt."),
})
json.dump(p, open(out, "w"), indent=2)
print("[capture] wrote", out)
print(json.dumps(p, indent=2))
PY

# --- image sanity: report dimensions + non-black fraction ---
python3 - "$OUTDIR/mc_frame.png" <<'PY' || true
import sys
try:
    from PIL import Image
except Exception:
    print("[capture] PIL unavailable; skipping histogram"); sys.exit(0)
im = Image.open(sys.argv[1]).convert("RGB")
px = list(im.getdata())
n = len(px)
black = sum(1 for r, g, b in px if r < 8 and g < 8 and b < 8)
uniq = len(set(px))
print("[capture] frame %dx%d  unique_colors=%d  black_frac=%.3f" %
      (im.size[0], im.size[1], uniq, black / n))
PY

# --- 8. stop the game cleanly ---
log "stopping game..."
pkill -9 -f '[G]radleStart' 2>/dev/null
pkill -9 -f '[r]unClient'   2>/dev/null
log "done. outputs: $OUTDIR/mc_frame.png , $OUTDIR/pose.json"
