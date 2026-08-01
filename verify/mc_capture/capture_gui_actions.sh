#!/usr/bin/env bash
# Capture the real MC 1.11.2 player inventory after each operation in a fixed
# eight-step sequence. Absolute GUI cursor input goes through mcwindow_script;
# framebuffer PNGs come from the qrl bridge's client-thread screenshot command.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
JAVA_DIR="$ROOT/java"
PORT=25582
export JAVA_HOME=/usr/lib/jvm/java-8-openjdk-amd64
export PATH="$JAVA_HOME/bin:$PATH"
export DISPLAY=:1

log() { echo "[capture_gui_actions] $*"; }
fail() { echo "[capture_gui_actions] FAIL: $*" >&2; exit 1; }

exec 9>/tmp/qrl_25575.lock
log "waiting for exclusive oracle lock /tmp/qrl_25575.lock ..."
flock 9
log "oracle lock acquired"

tmpdir=$(mktemp -d)
server_pid=""
started_oracle=0
cleanup() {
    if [ -n "$server_pid" ]; then kill "$server_pid" 2>/dev/null || true; fi
    if [ "$started_oracle" -eq 1 ]; then
        pkill -9 -f '[G]radleStart' 2>/dev/null || true
        pkill -9 -f '[r]unClient' 2>/dev/null || true
    fi
    rm -rf "$tmpdir"
}
trap cleanup EXIT

qrl_listening() {
    uv run --no-project python - <<'PY'
import socket
s = socket.socket()
s.settimeout(0.5)
try:
    s.connect(("127.0.0.1", 25575))
except OSError:
    raise SystemExit(1)
finally:
    s.close()
PY
}

if ! qrl_listening; then
    log "launching Java oracle on Xvfb :1"
    setsid bash "$JAVA_DIR/start_vnc_client.sh" > /tmp/mc_gui_actions_launch.out 2>&1 9>&-
    started_oracle=1
    for _ in $(seq 1 360); do
        qrl_listening && break
        sleep 1
    done
    qrl_listening || fail "qrl bridge did not start within 360s"
else
    log "reusing the locked Java oracle already listening on :25575"
fi

log "resetting deterministic inventory scene"
cd "$JAVA_DIR"
uv run --no-project python - "$SCRIPT_DIR/gui_actions_scene.json" <<'PY'
import json
import sys
import qrl_client

e = qrl_client.NetheriteEnv()
o = e.reset({"seed": 0, "mode": "survival", "type": "default"})
if not o.get("ok"):
    raise SystemExit(f"reset failed: {o}")
cmds = [
    "gamerule sendCommandFeedback false",
    "gamerule logAdminCommands false",
    "gamerule doDaylightCycle false",
    "gamerule doWeatherCycle false",
    "gamerule doMobSpawning false",
    "time set 6000",
    "weather clear 1000000",
    "clear @p",
    "achievement give achievement.openInventory @p",
    "replaceitem entity @p slot.inventory.0 minecraft:stone 2 0",
    "replaceitem entity @p slot.hotbar.1 minecraft:dirt 5 0",
]
# One runcmds batch executes in a single server tick, and "clear @p" on an
# already-empty inventory reports failure in 1.11; run commands one at a
# time with settle ticks and let only clear fail.
for cmd in cmds:
    r = e._cmd({"cmd": "runcmds", "action": {"cmds": [cmd]}})
    if (not r.get("ok") or r.get("failed")) and not cmd.startswith("clear "):
        raise SystemExit(f"scene command failed: {cmd!r} -> {r}")
    for _ in range(2):
        e.step({})
for _ in range(20):
    e.step({})
o = e.obs()
json.dump({"seed": 0, "loadout": {"inventory.0": [1, 0, 2],
          "hotbar.1": [3, 0, 5]}, "initial_inventory": o.get("inventory"),
          "width": 854, "height": 480,
          "steps": ["initial", "pickup_a", "place_b", "split_b",
                    "deposit_one_c", "shift_b_to_hotbar",
                    "swap_hotbar_0_1", "drop_one_hotbar0", "close"]},
          open(sys.argv[1], "w"), indent=2)
e.close()
PY

log "starting isolated mcwindow input relay on :$PORT"
MCW_PORT=$PORT MCW_W=854 MCW_H=480 DISPLAY=:1 \
    uv run --no-project --with python-xlib python "$JAVA_DIR/mcwindow_server.py" \
    > /tmp/mc_gui_actions_mcwindow.out 2>&1 &
server_pid=$!
for _ in $(seq 1 120); do
    ss -ltn 2>/dev/null | rg -q ":$PORT " && break
    kill -0 "$server_pid" 2>/dev/null || fail "mcwindow relay exited; see /tmp/mc_gui_actions_mcwindow.out"
    sleep 0.25
done
ss -ltn 2>/dev/null | rg -q ":$PORT " || fail "mcwindow relay did not listen on :$PORT"

drive() {
    local name=$1
    shift
    local script="$tmpdir/$name.jsonl"
    uv run --no-project python - "$script" "$@" <<'PY'
import json
import sys

path, cursor, button, key = sys.argv[1:]
x, y = map(int, cursor.split(","))
rows = [{"seconds": 0.20, "cursor": [x, y]}]
if button != "-":
    rows += [{"seconds": 0.10, "cursor": [x, y], "buttons": [int(button)]},
             {"seconds": 0.35, "cursor": [x, y]}]
elif key != "-":
    rows += [{"seconds": 0.10, "cursor": [x, y], "keys": [key]},
             {"seconds": 0.35, "cursor": [x, y]}]
with open(path, "w") as f:
    for row in rows:
        f.write(json.dumps(row, separators=(",", ":")) + "\n")
PY
    uv run --no-project python "$JAVA_DIR/mcwindow_script.py" --port "$PORT" "$script" >/dev/null
}

capture() {
    local name=$1
    uv run --no-project python - "$SCRIPT_DIR/mc_gui_action_${name}.png" <<'PY'
import sys
import qrl_client

e = qrl_client.NetheriteEnv()
r = e._cmd({"cmd": "frame", "action": {"file": sys.argv[1]}})
if not r.get("ok") or (r.get("w"), r.get("h")) != (854, 480):
    raise SystemExit(f"frame capture failed or wrong size: {r}")
e.close()
PY
    log "captured $name"
}

# Freeze ModelBiped ageInTicks for the inventory player preview (same pin as
# capture_gui.sh) so action frames share one idle pose and 00 gets a real _b.
uv run --no-project python - <<'PY'
import qrl_client
e = qrl_client.NetheriteEnv()
r = e._cmd({"cmd": "pin_preview_anim",
            "action": {"enable": True, "ticks_existed": -1}})
if not r.get("ok"):
    raise SystemExit(f"pin_preview_anim failed: {r}")
e.close()
print("[capture_gui_actions] pin_preview_anim enable ticks_existed=-1")
PY

# Open inventory, then drive framebuffer-space slot centers at GUI scale 2.
# Pose2 inventory goldens live only in capture_gui.sh (empty inv); this script
# must not overwrite mc_gui_inventory_pose2_{a,b}.png with the action loadout.
drive open "5,5" - e
drive hover_a "282,258" - -
capture 00_initial
drive pickup_a "282,258" 1 -
capture 01_pickup_a
drive place_b "318,258" 1 -
capture 02_place_b
drive split_b "318,258" 3 -
capture 03_split_b
drive deposit_one_c "354,258" 3 -
capture 04_deposit_one_c

# Shift-click B. Holding Shift and button 1 in one segment maps to QUICK_MOVE.
uv run --no-project python - "$tmpdir/shift.jsonl" <<'PY'
import json, sys
rows = [
    {"seconds": .2, "cursor": [318, 258]},
    {"seconds": .1, "cursor": [318, 258], "keys": ["Shift_L"], "buttons": [1]},
    {"seconds": .35, "cursor": [318, 258]},
]
with open(sys.argv[1], "w") as f:
    for row in rows: f.write(json.dumps(row, separators=(",", ":")) + "\n")
PY
uv run --no-project python "$JAVA_DIR/mcwindow_script.py" --port "$PORT" "$tmpdir/shift.jsonl" >/dev/null
capture 05_shift_b_to_hotbar

# Logical hotbar swap uses the same three PICKUP operations as magma.
drive swap_1 "282,374" 1 -
drive swap_2 "318,374" 1 -
drive swap_3 "282,374" 1 -
capture 06_swap_hotbar_0_1
drive drop_one "282,374" - q
capture 07_drop_one_hotbar0
drive close "282,374" - e
capture 08_close

uv run --no-project python - <<'PY'
import qrl_client
e = qrl_client.NetheriteEnv()
e._cmd({"cmd": "pin_preview_anim", "action": {"enable": False}})
e.close()
print("[capture_gui_actions] pin_preview_anim disabled")
PY

uv run --no-project python - "$SCRIPT_DIR/gui_actions_scene.json" <<'PY'
import json
import sys
import qrl_client

path = sys.argv[1]
e = qrl_client.NetheriteEnv()
diag = e._cmd({"cmd": "focusdiag", "action": {}})
e.close()
data = json.load(open(path))
data["close_focusdiag"] = diag
json.dump(data, open(path, "w"), indent=2)
if not diag.get("ok") or diag.get("screen") is not None:
    raise SystemExit(f"inventory did not close: {diag}")
PY

log "done: mc_gui_action_00_initial.png through mc_gui_action_08_close.png"
