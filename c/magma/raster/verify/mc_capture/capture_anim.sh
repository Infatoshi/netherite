#!/usr/bin/env bash
# Capture an every-tick REAL Minecraft animation tape for run_anim_verify.sh.
# The qrl lock covers config, launch, scene construction, recording, and cleanup.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../../../.." && pwd)"
JAVA="$ROOT/java"
TRACE="$ROOT/c/magma/raster/verify/trace"
OUT="$HERE/anim_fixture"
RUN="$JAVA/Minecraft/run"
TMP="$(mktemp -d)"
STARTED=0

exec 9>/tmp/qrl_25575.lock
echo "[anim-capture] waiting for exclusive qrl :25575 lock"
flock 9
echo "[anim-capture] qrl lock acquired"

cleanup() {
	local rc=$?
	if [ "$STARTED" = 1 ]; then
		pkill -9 -f '[G]radleStart' 2>/dev/null || true
		pkill -9 -f '[r]unClient' 2>/dev/null || true
		pkill -9 -f '[X]vfb :1' 2>/dev/null || true
		pkill -9 -f '[x]11vnc' 2>/dev/null || true
		pkill -9 -f '[o]penbox' 2>/dev/null || true
	fi
	[ ! -f "$TMP/options.txt" ] || cp "$TMP/options.txt" "$RUN/options.txt"
	[ ! -f "$TMP/qrl_launch.json" ] || cp "$TMP/qrl_launch.json" "$RUN/qrl_launch.json"
	rm -rf "$TMP"
	echo "[anim-capture] qrl lock released"
	exit "$rc"
}
trap cleanup EXIT INT TERM

mkdir -p "$OUT"
[ ! -f "$RUN/options.txt" ] || cp "$RUN/options.txt" "$TMP/options.txt"
[ ! -f "$RUN/qrl_launch.json" ] || cp "$RUN/qrl_launch.json" "$TMP/qrl_launch.json"

uv run --no-project --with pyyaml python "$JAVA/mc_cli.py" \
	--config "$JAVA/fast.yaml" \
	--set determinism.pin_texture_animations=false \
	--set ui.hide_gui=true --no-launch

setsid bash "$JAVA/start_vnc_client.sh" >"$OUT/launch.log" 2>&1 </dev/null 9>&- &
STARTED=1
echo "[anim-capture] waiting for qrl bridge"
ready=0
for _ in $(seq 1 420); do
	if nc -z 127.0.0.1 25575 2>/dev/null; then
		ready=1
		break
	fi
	sleep 1
done
[ "$ready" = 1 ] || {
	tail -n 80 "$JAVA/runclient.log" >&2
	exit 1
}

echo "[anim-capture] building fixed scene"
cd "$ROOT"
uv run --no-project python - "$OUT/scene_setup.json" <<'PY'
import json
import sys

sys.path.insert(0, "java")
from qrl_client import QRLEnv

out = sys.argv[1]
e = QRLEnv()
o = e.reset({"seed": 0, "mode": "survival", "type": "default", "fresh": True},
            timeout=300.0)
if not o.get("ok"):
    raise SystemExit(f"fresh reset failed: {o}")

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
    "fill 188 96 190 218 112 218 minecraft:air",
    "fill 188 99 190 218 99 218 minecraft:stone",
    # Deep still-water pool on the left. The y=100..102 source volume also
    # provides the fixed underwater-overlay pose.
    "fill 190 100 198 197 102 207 minecraft:water",
    # Flowing-water cascade in the center. Let scheduled liquid updates settle
    # before recording so geometry is static while its atlas sprite advances.
    "fill 200 100 199 204 100 207 minecraft:stone",
    "fill 201 101 199 203 105 199 minecraft:stone",
    "setblock 202 105 200 minecraft:water",
    # Direct portal blocks avoid ignition timing while retaining the real pane.
    "fill 207 100 201 210 100 201 minecraft:obsidian",
    "fill 207 104 201 210 104 201 minecraft:obsidian",
    "fill 207 101 201 207 103 201 minecraft:obsidian",
    "fill 210 101 201 210 103 201 minecraft:obsidian",
    "fill 208 101 201 209 103 201 minecraft:portal 1",
    # Lava and fire are present so the capture also proves their current gap.
    "fill 212 100 201 215 100 205 minecraft:netherrack",
    "fill 212 101 202 213 101 204 minecraft:lava",
    "setblock 215 101 203 minecraft:fire",
    "tp @a 203.5 100 216.5 180 12",
]
r = e._cmd({"cmd": "runcmds", "action": {"cmds": cmds}})
if r.get("failed", 1) != 0:
    raise SystemExit(f"scene commands failed: {r}")
for _ in range(120):
    e._cmd({"cmd": "set_pose", "action": {
        "x": 203.5, "y": 100.0, "z": 216.5, "yaw": 180.0, "pitch": 12.0,
    }})
    e.step({})
cam = e.camera()
if cam.get("texture_animations_pinned") is not False:
    raise SystemExit(f"texture animations are still pinned: {cam}")
dump = e._cmd({"cmd": "dumpblocks", "action": {
    "radius": 2, "file": out.replace("scene_setup.json", "scene.mcbd"),
}})
json.dump({"camera": cam, "dump": dump, "commands": cmds}, open(out, "w"), indent=2)
e.close()
PY

uv run --no-project --with pyarrow python "$TRACE/tape.py" start \
	--frames-every 1 --seed 0 --dir "$OUT"

echo "[anim-capture] recording wide animation burst and underwater pose"
uv run --no-project python - <<'PY'
import sys
sys.path.insert(0, "java")
from qrl_client import QRLEnv

e = QRLEnv()
poses = [
    (72, 203.5, 100.0, 216.5, 180.0, 12.0),
    (12, 194.5, 100.0, 202.5, 180.0, 8.0),
]
for count, x, y, z, yaw, pitch in poses:
    for _ in range(count):
        r = e._cmd({"cmd": "set_pose", "action": {
            "x": x, "y": y, "z": z, "yaw": yaw, "pitch": pitch,
        }})
        if r.get("ok") is not True:
            raise SystemExit(f"set_pose failed: {r}")
        e.step({})
e.close()
PY

uv run --no-project --with pyarrow python "$TRACE/tape.py" stop --dir "$OUT"
echo "[anim-capture] fixture written under $OUT"
