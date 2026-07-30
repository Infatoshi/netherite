#!/usr/bin/env bash
# capture_ui_hud.sh - capture REAL Minecraft 1.11.2 HUD / viewmodel / overlay
# goldens listed in ORACLE_CAPTURE.md.
#
# For every state ID, dumps twin frames (_a / _b) via the qrl "frame" command
# (tick-boundary re-render at partialTicks=1 with HUD). Camera + vitals are
# frozen with set_pose / hud_pin so A/B noise is meaningful.
#
# Never synthesizes or edits PNG contents. Writes:
#   <id>_a.png, <id>_b.png, meta/<id>.json, capture_manifest.json
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
ENVDIR="$ROOT/java"
OUTDIR="$SCRIPT_DIR/goldens"
METADIR="$OUTDIR/meta"
OPTS="$ENVDIR/Minecraft/run/options.txt"
LAUNCH_JSON="$ENVDIR/Minecraft/run/qrl_launch.json"
LAUNCH_JSON_BAK="/tmp/qrl_launch_ui_hud_bak.json"
LAUNCH_LOG=/tmp/mc_ui_hud_launch.out
RUNCLIENT_LOG="$ENVDIR/runclient.log"
SEED=0
export JAVA_HOME="${JAVA_HOME:-/tmp/netherite-jdk8.hO5KUx/root/usr/lib/jvm/java-8-openjdk-amd64}"
if [ ! -x "$JAVA_HOME/bin/java" ]; then
  JAVA_HOME=/usr/lib/jvm/java-8-openjdk-amd64
fi
export PATH="$JAVA_HOME/bin:$PATH"
export DISPLAY=:1
export LIBGL_ALWAYS_SOFTWARE=1
export MESA_GL_VERSION_OVERRIDE=2.1

log() { echo "[capture_ui_hud] $*"; }
fail() {
  echo "[capture_ui_hud] FAIL: $*" >&2
  echo "----- runclient.log tail -----" >&2
  tail -n 60 "$RUNCLIENT_LOG" 2>/dev/null >&2
  echo "----- launch log tail -----" >&2
  tail -n 40 "$LAUNCH_LOG" 2>/dev/null >&2
  exit 1
}

mkdir -p "$OUTDIR" "$METADIR"

# One process owns the Java oracle, qrl port, and Xvfb :1.
exec 9>/tmp/qrl_25575.lock
log "waiting for exclusive oracle lock /tmp/qrl_25575.lock ..."
flock 9
log "oracle lock acquired"

cleanup() {
  if [ -f "$LAUNCH_JSON_BAK" ]; then
    mv -f "$LAUNCH_JSON_BAK" "$LAUNCH_JSON" 2>/dev/null || true
  fi
  pkill -9 -f '[G]radleStart' 2>/dev/null || true
  pkill -9 -f '[r]unClient' 2>/dev/null || true
}
trap cleanup EXIT

# Capture launch profile: death screen + boss bar must be visible.
if [ -f "$LAUNCH_JSON" ]; then
  cp -f "$LAUNCH_JSON" "$LAUNCH_JSON_BAK"
fi
cat >"$LAUNCH_JSON" <<'JSON'
{
  "port": 25575,
  "profile": "ui_hud_oracle",
  "chat": false,
  "hide_gui": false,
  "strip": {
    "menus": false,
    "overlays": false,
    "sound": true
  },
  "determinism": {
    "pin_flicker": true,
    "pin_skin": true,
    "pin_texture_animations": true
  }
}
JSON
log "qrl_launch.json: strip.menus=false strip.overlays=false (death+boss visible)"

# Ensure gui scale 2 / bob off / no clouds (match capture_gui profile).
if [ -f "$OPTS" ]; then
  sed -i 's/^guiScale:.*/guiScale:2/' "$OPTS"
  sed -i 's/^bobView:.*/bobView:false/' "$OPTS"
  sed -i 's/^renderClouds:.*/renderClouds:false/' "$OPTS"
  sed -i 's/^fancyGraphics:.*/fancyGraphics:false/' "$OPTS"
  sed -i 's/^renderDistance:.*/renderDistance:8/' "$OPTS"
  sed -i 's/^pauseOnLostFocus:.*/pauseOnLostFocus:false/' "$OPTS"
fi

log "killing any running game..."
pkill -9 -f '[G]radleStart' 2>/dev/null || true
pkill -9 -f '[r]unClient' 2>/dev/null || true
sleep 2
SAVE="$ENVDIR/Minecraft/run/saves/qrl_${SEED}"
[ -d "$SAVE" ] && { log "wiping stale save $SAVE"; rm -rf "$SAVE"; }
# Also wipe flat variant if present
rm -rf "$ENVDIR/Minecraft/run/saves/qrl_${SEED}_flat" 2>/dev/null || true

log "launching headless game via start_vnc_client.sh (JAVA_HOME=$JAVA_HOME)..."
( cd "$ENVDIR" && setsid nohup bash start_vnc_client.sh >"$LAUNCH_LOG" 2>&1 </dev/null 9>&- & )

log "waiting for qrl bridge on :25575 with hud_pin (up to 420s)..."
listened=0
for i in $(seq 1 420); do
  if uv run --no-project python -c '
import socket, sys, json
s = socket.socket(); s.settimeout(3.0)
try:
    s.connect(("127.0.0.1", 25575))
except Exception:
    sys.exit(1)
# Newline-delimited JSON protocol (see java/qrl_client.py).
# Probe that this is OUR Recorder (hud_pin exists), not a stale bridge.
try:
    s.sendall((json.dumps({"cmd": "hud_pin", "action": {}}) + "\n").encode())
    buf = b""
    while b"\n" not in buf:
        chunk = s.recv(65536)
        if not chunk:
            break
        buf += chunk
    s.close()
    if b"\n" not in buf:
        sys.exit(2)
    line = buf.split(b"\n", 1)[0]
    r = json.loads(line.decode("utf-8", "replace"))
    err = str(r.get("error", "") or "")
    if "unknown cmd" in err.lower():
        sys.exit(3)
    # ok / no world / other real hud_pin errors all mean the cmd is registered
    sys.exit(0)
except Exception:
    try: s.close()
    except Exception: pass
    sys.exit(2)
' 2>/dev/null; then
    listened=1
    break
  fi
  sleep 1
done
[ "$listened" = 1 ] || fail "qrl bridge never accepted hud_pin within 420s"
log "bridge up (hud_pin recognized)."

ONLY_ARGS=()
REQUIRED=(
  hud_armor_iron hud_absorption_armor hud_hurt_flash_on hud_hurt_flash_off
  hud_hunger_poison hud_air_partial hud_xp_half hud_durability_half
  hud_boss_half hud_death
  hand_bow_pull20 hand_eat_mid hand_block_shield
  overlay_inside_stone overlay_inside_grass overlay_portal_050
  overlay_fire overlay_underwater
)
# Optional: ONLY=hand bash capture_ui_hud.sh  (or ONLY=hand_bow_pull20,...)
if [ -n "${ONLY:-}" ]; then
  ONLY_ARGS=(--only "$ONLY")
  if [ "$ONLY" = "hand" ] || [ "$ONLY" = "hands" ] || [ "$ONLY" = "viewmodel" ]; then
    REQUIRED=(hand_bow_pull20 hand_eat_mid hand_block_shield)
  else
    # comma list
    IFS=',' read -r -a REQUIRED <<< "$ONLY"
  fi
  log "ONLY=$ONLY -> required: ${REQUIRED[*]}"
fi

log "running capture driver..."
cd "$ENVDIR" || fail "cd $ENVDIR"
# Driver enforces A/B noise + feature presence (needs pillow/numpy).
uv run --no-project --with pillow --with numpy python \
  "$SCRIPT_DIR/capture_ui_hud_driver.py" \
  --out "$OUTDIR" \
  --seed "$SEED" \
  "${ONLY_ARGS[@]}" \
  || fail "capture driver failed"

# Sanity: every required ID has both frames non-empty
missing=0
for id in "${REQUIRED[@]}"; do
  id="$(echo "$id" | tr -d '[:space:]')"
  [ -n "$id" ] || continue
  for ab in a b; do
    f="$OUTDIR/${id}_${ab}.png"
    if [ ! -s "$f" ]; then
      echo "[capture_ui_hud] missing $f" >&2
      missing=1
    fi
  done
done
# Contaminated legacy name from pre-audit sword-block captures.
if [ -f "$OUTDIR/hand_block_sword_a.png" ] || [ -f "$OUTDIR/hand_block_sword_b.png" ]; then
  echo "[capture_ui_hud] FAIL: contaminated hand_block_sword_* still present" >&2
  missing=1
fi
[ "$missing" = 0 ] || fail "one or more goldens missing or contaminated"

log "done: $(ls "$OUTDIR"/*_a.png 2>/dev/null | wc -l) states x2 frames under $OUTDIR"
log "oracle cleanup runs on exit"
