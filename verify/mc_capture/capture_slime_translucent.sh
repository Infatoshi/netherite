#!/usr/bin/env bash
# capture_slime_translucent.sh - live MC 1.11.2 translucent DRAW dump of the
# slime_bounce pad (quad buffer after sortVertexData + model census + A/B frame).
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
ENVDIR="$ROOT/java"
OUTDIR="$ROOT/verify/fixtures/slime_translucent"
OPTS="$ENVDIR/Minecraft/run/options.txt"
LAUNCH_JSON="$ENVDIR/Minecraft/run/qrl_launch.json"
LAUNCH_JSON_BAK="/tmp/qrl_launch_slime_trans_bak.json"
LAUNCH_LOG=/tmp/mc_slime_trans_launch.out
RUNCLIENT_LOG="$ENVDIR/runclient.log"
SEED=0
export JAVA_HOME="${JAVA_HOME:-/usr/lib/jvm/java-8-openjdk-amd64}"
if [ ! -x "$JAVA_HOME/bin/java" ]; then
  JAVA_HOME=/usr/lib/jvm/java-8-openjdk-amd64
fi
export PATH="$JAVA_HOME/bin:$PATH"
export DISPLAY=:1
export LIBGL_ALWAYS_SOFTWARE=1
export MESA_GL_VERSION_OVERRIDE=2.1

log() { echo "[capture_slime_translucent] $*"; }
fail() {
  echo "[capture_slime_translucent] FAIL: $*" >&2
  tail -n 60 "$RUNCLIENT_LOG" 2>/dev/null >&2 || true
  tail -n 40 "$LAUNCH_LOG" 2>/dev/null >&2 || true
  exit 1
}

mkdir -p "$OUTDIR"

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

if [ -f "$LAUNCH_JSON" ]; then
  cp -f "$LAUNCH_JSON" "$LAUNCH_JSON_BAK"
fi
cat >"$LAUNCH_JSON" <<'JSON'
{
  "port": 25575,
  "profile": "slime_translucent_oracle",
  "chat": false,
  "hide_gui": false,
  "strip": {
    "menus": true,
    "overlays": true,
    "sound": true
  },
  "determinism": {
    "pin_flicker": true,
    "pin_skin": true,
    "pin_texture_animations": true
  }
}
JSON

if [ -f "$OPTS" ]; then
  sed -i 's/^guiScale:.*/guiScale:2/' "$OPTS"
  sed -i 's/^bobView:.*/bobView:false/' "$OPTS"
  sed -i 's/^renderClouds:.*/renderClouds:false/' "$OPTS"
  sed -i 's/^fancyGraphics:.*/fancyGraphics:false/' "$OPTS"
  sed -i 's/^renderDistance:.*/renderDistance:8/' "$OPTS"
  sed -i 's/^pauseOnLostFocus:.*/pauseOnLostFocus:false/' "$OPTS"
  sed -i 's/^particles:.*/particles:0/' "$OPTS"
fi

log "killing any running game..."
pkill -9 -f '[G]radleStart' 2>/dev/null || true
pkill -9 -f '[r]unClient' 2>/dev/null || true
sleep 2
rm -rf "$ENVDIR/Minecraft/run/saves/qrl_${SEED}" "$ENVDIR/Minecraft/run/saves/qrl_${SEED}_flat" 2>/dev/null || true

log "launching headless game via start_vnc_client.sh (JAVA_HOME=$JAVA_HOME)..."
( cd "$ENVDIR" && setsid nohup bash start_vnc_client.sh >"$LAUNCH_LOG" 2>&1 </dev/null 9>&- & )

log "waiting for qrl bridge on :25575 (up to 420s)..."
listened=0
for i in $(seq 1 420); do
  if uv run --no-project python -c '
import socket, sys, json
s = socket.socket(); s.settimeout(3.0)
try:
    s.connect(("127.0.0.1", 25575))
except Exception:
    sys.exit(1)
try:
    s.sendall((json.dumps({"cmd": "obs", "action": {}}) + "\n").encode())
    buf = b""
    while b"\n" not in buf:
        chunk = s.recv(65536)
        if not chunk:
            break
        buf += chunk
    s.close()
    sys.exit(0 if b"\n" in buf else 2)
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
[ "$listened" = 1 ] || fail "qrl bridge never accepted obs within 420s"
log "bridge up."

uv run --no-project --with numpy --with pillow python \
  "$SCRIPT_DIR/capture_slime_translucent.py" --outdir "$OUTDIR" --seed "$SEED" \
  || fail "driver failed"

log "wrote $OUTDIR"
ls -l "$OUTDIR" | head
