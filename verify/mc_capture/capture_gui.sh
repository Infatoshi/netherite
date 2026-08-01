#!/usr/bin/env bash
# capture_gui.sh - capture the REAL Minecraft 1.11.2 container screens (player
# inventory / crafting table / furnace / single chest) as pixel goldens for
# magma's gm_screen_draw (run_gui_verify.sh diffs the panel region).
#
# Scene: fresh seed-0 qrl world, survival, EMPTY inventory, frozen clear noon,
# a stone platform at a fixed spot with a crafting table + furnace + chest
# placed on it, GUI scale 2 (854x480 window). Each screen is grabbed TWICE
# (_a/_b) so the verifier can calibrate the Java-vs-Java noise floor from
# repeat frames.
#
# Input mechanics (headless Xvfb): keyboard XTEST events reach LWJGL2 but
# synthetic/XTEST MOUSE clicks and the qrl use-keybind do NOT trigger
# rightClickMouse. So this script REBINDS key.use to R (lwjgl 19) in
# options.txt before launching and opens the block GUIs with `xdotool key r`
# while the crosshair aims at the block; the player screen opens with E.
# The original options.txt binding is restored on exit.
#
# Anti-rabbit-hole: every wait has a hard timeout; failures print log tails.
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../../../.." && pwd)"
ENVDIR="$ROOT/java"
OUTDIR="$SCRIPT_DIR"
OPTS="$ENVDIR/Minecraft/run/options.txt"
export JAVA_HOME=/usr/lib/jvm/java-8-openjdk-amd64
export PATH="$JAVA_HOME/bin:$PATH"
export DISPLAY=:1

SEED=0
# platform + blocks (all absolute; spawn region of seed 0's qrl_world)
PLAT_Y=86
TABLE="16 87 266"
FURNACE="17 87 266"
CHEST="15 87 266"
POSE_STAND="16.5 87.0 268.5"
AIM_TABLE="180 29.3"    # crosshair on the crafting table from POSE_STAND
AIM_FURNACE="206.6 26.6" # crosshair on the furnace from POSE_STAND
AIM_CHEST="153.4 29.3"   # crosshair on the single chest from POSE_STAND
LAUNCH_LOG=/tmp/mc_gui_launch.out
RUNCLIENT_LOG="$ENVDIR/runclient.log"

log() { echo "[capture_gui] $*"; }
fail() { echo "[capture_gui] FAIL: $*" >&2; echo "----- runclient.log tail -----" >&2; tail -n 40 "$RUNCLIENT_LOG" 2>/dev/null >&2; exit 1; }

mkdir -p "$OUTDIR"

# One process may own the Java oracle, qrl port, and Xvfb :1 at a time.
exec 9>/tmp/qrl_25575.lock
log "waiting for exclusive oracle lock /tmp/qrl_25575.lock ..."
flock 9
log "oracle lock acquired"

# --- 0. rebind key.use to R (restored on exit) ---
rg -q '^key_key.use:' "$OPTS" || fail "options.txt has no key_key.use line"
ORIG_USE=$(rg '^key_key.use:' "$OPTS" | head -1 | cut -d: -f2)
cleanup() {
    sed -i "s/^key_key.use:.*/key_key.use:${ORIG_USE}/" "$OPTS" 2>/dev/null
    pkill -9 -f '[G]radleStart' 2>/dev/null || true
    pkill -9 -f '[r]unClient' 2>/dev/null || true
}
trap cleanup EXIT
sed -i 's/^key_key.use:.*/key_key.use:19/' "$OPTS"
log "key.use rebound: ${ORIG_USE} -> 19 (R); will restore on exit"

# --- 1. kill any old game, wipe the seed save for a clean empty-inventory world ---
log "killing any running game..."
pkill -9 -f '[G]radleStart' 2>/dev/null
pkill -9 -f '[r]unClient'   2>/dev/null
sleep 3
SAVE="$ENVDIR/Minecraft/run/saves/qrl_${SEED}"
[ -d "$SAVE" ] && { log "wiping stale save $SAVE"; rm -rf "$SAVE"; }

# --- 2. launch fresh headless stack + game ---
log "launching headless game via start_vnc_client.sh ..."
( cd "$ENVDIR" && setsid nohup bash start_vnc_client.sh >"$LAUNCH_LOG" 2>&1 </dev/null 9>&- & )

# --- 3. wait for the qrl bridge (real socket probe, never log grep) ---
log "waiting for qrl bridge on :25575 (up to 360s)..."
listened=0
for i in $(seq 1 360); do
    if uv run --no-project python -c 'import socket,sys; s=socket.socket(); s.settimeout(1)
try:
    s.connect(("127.0.0.1",25575)); s.close()
except Exception:
    sys.exit(1)' 2>/dev/null; then listened=1; break; fi
    sleep 1
done
[ "$listened" = 1 ] || fail "qrl bridge never accepted a connection within 360s"

# --- 4. reset + freeze + build the fixed GUI scene ---
log "reset(seed=$SEED) + frozen noon + platform + table/furnace ..."
cd "$ENVDIR" || fail "cd $ENVDIR"
uv run --no-project python - "$SEED" <<PY || fail "qrl scene setup failed"
import sys, json
import qrl_client
e = qrl_client.NetheriteEnv()
o = e.reset({"seed": int(sys.argv[1]), "mode": "survival", "type": "default"})
if not o.get("ok"):
    print("reset not ok:", o, file=sys.stderr); sys.exit(1)
cmds = [
    "gamerule sendCommandFeedback false",
    "gamerule logAdminCommands false",
    "gamerule doDaylightCycle false",
    "gamerule doWeatherCycle false",
    "gamerule doMobSpawning false",
    "gamerule doFireTick false",
    "gamerule randomTickSpeed 0",
    "time set 6000",
    "weather clear 1000000",
    "fill 14 $PLAT_Y 266 19 $PLAT_Y 270 minecraft:stone",
    "setblock $TABLE minecraft:crafting_table",
    "setblock $FURNACE minecraft:furnace",
    "setblock $CHEST minecraft:chest",
    "tp @a $POSE_STAND $AIM_TABLE",
]
r = e._cmd({"cmd": "runcmds", "action": {"cmds": cmds}})
print("[py] runcmds:", r, file=sys.stderr)
if r.get("failed", 1) != 0:
    print("some setup commands failed:", r, file=sys.stderr); sys.exit(1)
for _ in range(40):
    e.step({})
o = e.obs()
look = o.get("look") or {}
if look.get("block") != "tile.workbench":
    print("crosshair not on workbench:", look, file=sys.stderr); sys.exit(1)
if o.get("inventory"):
    print("inventory not empty (goldens must be empty):", o["inventory"], file=sys.stderr)
    sys.exit(1)
with open("/tmp/qrl_gui_scene.json", "w") as f:
    json.dump({"seed": int(sys.argv[1]), "x": o["x"], "y": o["y"], "z": o["z"],
               "inventory": o.get("inventory"), "time": o.get("time")}, f)
e.close()
PY

# --- 5. locate the MC window ---
GEOM=$(xwininfo -root -tree 2>/dev/null | rg -i "Minecraft 1.11.2" | head -1)
WH=$(echo "$GEOM"  | rg -o '[0-9]+x[0-9]+\+[0-9]+\+[0-9]+' | head -1)
ABS=$(echo "$GEOM" | rg -o '\+[0-9]+\+[0-9]+$' | head -1)
W=$(echo "$WH" | cut -dx -f1)
H=$(echo "$WH" | sed -E 's/^[0-9]+x([0-9]+).*/\1/')
AX=$(echo "$ABS" | cut -d+ -f2)
AY=$(echo "$ABS" | cut -d+ -f3)
if [ -z "${W:-}" ] || [ -z "${AX:-}" ]; then fail "MC window geometry not found"; fi
log "MC window ${W}x${H} at (${AX},${AY})"
WIN=$(xdotool search --name "Minecraft 1.11.2" | head -1)
[ -n "$WIN" ] || fail "xdotool cannot find the MC window"
xdotool windowactivate --sync "$WIN"; sleep 1

# grab NAME twice (a + b) with the pointer parked at the window corner
grab2() {
    xdotool mousemove $((AX + 5)) $((AY + 5)); sleep 0.4
    ffmpeg -hide_banner -loglevel error -y -f x11grab -draw_mouse 0 \
        -video_size "${W}x${H}" -i ":1.0+${AX},${AY}" -frames:v 1 \
        "$OUTDIR/mc_gui_$1_a.png" || fail "grab $1_a"
    sleep 0.5
    ffmpeg -hide_banner -loglevel error -y -f x11grab -draw_mouse 0 \
        -video_size "${W}x${H}" -i ":1.0+${AX},${AY}" -frames:v 1 \
        "$OUTDIR/mc_gui_$1_b.png" || fail "grab $1_b"
    [ -s "$OUTDIR/mc_gui_$1_a.png" ] || fail "mc_gui_$1_a.png empty"
}

retp() { # re-assert a pose through qrl (tp + settle ticks)
    uv run --no-project python - "$1" <<'PY'
import sys
import qrl_client
e = qrl_client.NetheriteEnv()
e._cmd({"cmd": "runcmds", "action": {"cmds": ["tp @a " + sys.argv[1]]}})
for _ in range(10):
    e.step({})
e.close()
PY
}

# --- 6a. crafting table screen (crosshair already on the table; R = use) ---
log "opening crafting table screen ..."
xdotool key --window "$WIN" r; sleep 1.5
sleep 5   # let achievement toasts fade (they render top-right, but be safe)
grab2 table
xdotool key --window "$WIN" Escape; sleep 1

# --- 6b. furnace screen ---
log "opening furnace screen ..."
retp "$POSE_STAND $AIM_FURNACE"
xdotool key --window "$WIN" r; sleep 1.5
grab2 furnace
xdotool key --window "$WIN" Escape; sleep 1

# pin living-anim clock so inventory player-preview A/B share ageInTicks=0
pin_preview() {
    local en=$1
    uv run --no-project python - "$en" <<'PY'
import sys
import qrl_client
e = qrl_client.NetheriteEnv()
enable = sys.argv[1] == "1"
r = e._cmd({"cmd": "pin_preview_anim",
            "action": {"enable": enable, "ticks_existed": -1}})
if not r.get("ok"):
    raise SystemExit(f"pin_preview_anim failed: {r}")
e.close()
PY
}

# --- 6c. player inventory screen (E), preview anim pinned ---
log "opening player inventory screen (pin_preview_anim ageInTicks=0) ..."
retp "$POSE_STAND $AIM_TABLE"
xdotool key --window "$WIN" e; sleep 1.5
pin_preview 1
# pose1: mouse parked at window (5,5) — look-at near panel corner
grab2 inventory
# pose2: mouse on inv slot A center (fb 282,258 at 854x480 scale-2)
log "inventory preview pose2 (mouse on inv slot A) ..."
xdotool mousemove $((AX + 282)) $((AY + 258)); sleep 0.4
ffmpeg -hide_banner -loglevel error -y -f x11grab -draw_mouse 0 \
    -video_size "${W}x${H}" -i ":1.0+${AX},${AY}" -frames:v 1 \
    "$OUTDIR/mc_gui_inventory_pose2_a.png" || fail "grab inventory_pose2_a"
sleep 0.5
ffmpeg -hide_banner -loglevel error -y -f x11grab -draw_mouse 0 \
    -video_size "${W}x${H}" -i ":1.0+${AX},${AY}" -frames:v 1 \
    "$OUTDIR/mc_gui_inventory_pose2_b.png" || fail "grab inventory_pose2_b"
[ -s "$OUTDIR/mc_gui_inventory_pose2_a.png" ] || fail "mc_gui_inventory_pose2_a.png empty"
pin_preview 0
xdotool key --window "$WIN" Escape; sleep 1

# --- 6d. single-chest screen ---
log "opening single chest screen ..."
retp "$POSE_STAND $AIM_CHEST"
xdotool key --window "$WIN" r; sleep 1.5
grab2 chest
xdotool key --window "$WIN" Escape; sleep 1

# --- 7. metadata ---
uv run --no-project python - "$OUTDIR/gui_scene.json" "$W" "$H" <<'PY'
import sys, json
scene = json.load(open("/tmp/qrl_gui_scene.json"))
scene.update({"width": int(sys.argv[2]), "height": int(sys.argv[3]),
              "gui_scale": 2,
              "screens": ["table", "furnace", "inventory", "chest",
                          "inventory_pose2"],
              "preview_anim_pin": {"enable": True, "ticks_existed": -1,
                                   "age_in_ticks": 0.0,
                                   "note": "drawEntityOnScreen partialTicks=1"},
              "notes": ("Empty survival inventory, frozen clear noon. Inventory "
                        "preview uses pin_preview_anim (ticksExisted=-1 => "
                        "ageInTicks=0) so A/B grabs share idle arm Z=±0.10. "
                        "Pose1 mouse at window (5,5); pose2 at fb (282,258) slot A. "
                        "Each screen grabbed twice (_a/_b) for the repeat-capture "
                        "noise floor. key.use rebound to R. Chest empty (no loot).")})
json.dump(scene, open(sys.argv[1], "w"), indent=2)
PY

log "done: $OUTDIR/mc_gui_{table,furnace,inventory,chest}_{a,b}.png + pose2_{a,b} + gui_scene.json"
log "oracle cleanup runs on exit"
