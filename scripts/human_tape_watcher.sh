#!/usr/bin/env bash
# Arms recording for a human play session: waits for the qrl bridge (:25575),
# takes a baseline pose, starts the tape on the first movement/look, then
# auto-recstops after IDLE_SECS with no pose change (or client quit) and packs.
# Run on the machine the client runs on. Prints the finished tape path.
set -u
cd "$(cd "$(dirname "$0")/.." && pwd)"
export PATH="$HOME/.local/bin:$PATH"
IDLE_SECS=${IDLE_SECS:-300}
POLL=3

obs() {
  uv run --no-project python - <<'EOF' 2>/dev/null
import json, socket
try:
    s = socket.create_connection(("127.0.0.1", 25575), timeout=2)
    s.sendall((json.dumps({"cmd": "obs"}) + "\n").encode())
    buf = b""
    while not buf.endswith(b"\n"):
        chunk = s.recv(65536)
        if not chunk: break
        buf += chunk
    o = json.loads(buf)
    if o.get("ok") and "x" in o:
        yaw = o.get("yaw", 0.0); pitch = o.get("pitch", 0.0)
        print(f"{o['x']:.3f},{o['y']:.3f},{o['z']:.3f},{yaw:.2f},{pitch:.2f}")
except Exception:
    pass
EOF
}

echo "[watcher] waiting for baseline pose on :25575..."
base=""
while [ -z "$base" ]; do
  base=$(obs)
  [ -z "$base" ] && sleep $POLL
done
echo "[watcher] baseline pose $base; armed - recording starts on first movement/look"

while true; do
  sleep $POLL
  p=$(obs)
  [ -n "$p" ] && [ "$p" != "$base" ] && break
done
echo "[watcher] player moved ($p); recstart"
uv run --no-project python verify/trace/tape.py start --frames-every 20 || {
  echo "[watcher] FATAL: tape.py start failed"; exit 1; }
echo "[watcher] RECORDING"

last=""
idle=0
while true; do
  sleep $POLL
  p=$(obs)
  if [ -z "$p" ]; then
    echo "[watcher] WARNING: bridge stopped answering (client quit?)"
    idle=$((idle + POLL))
  elif [ "$p" = "$last" ]; then
    idle=$((idle + POLL))
  else
    idle=0
    last="$p"
  fi
  if [ $idle -ge $IDLE_SECS ]; then
    echo "[watcher] ${IDLE_SECS}s idle; stopping tape"
    break
  fi
done
uv run --no-project --with pyarrow python verify/trace/tape.py stop
echo "[watcher] tape stopped and packed; newest tape:"
ls -t verify/tapes/*.jsonl | head -1
