#!/usr/bin/env bash
# capture_ui_entities.sh - REAL MC 1.11.2 goldens for interactive entity paths.
# Twin A/B frames via qrl frame{} (partialTicks=1). Never fabricates PNGs.
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
ENVDIR="$ROOT/java"
OUTDIR="$SCRIPT_DIR/goldens"
METADIR="$OUTDIR/meta"
OPTS="$ENVDIR/Minecraft/run/options.txt"
LAUNCH_JSON="$ENVDIR/Minecraft/run/qrl_launch.json"
LAUNCH_JSON_BAK="/tmp/qrl_launch_ui_entities_bak.json"
LAUNCH_LOG=/tmp/mc_ui_entities_launch.out
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

log() { echo "[capture_ui_entities] $*"; }
fail() {
  echo "[capture_ui_entities] FAIL: $*" >&2
  tail -n 60 "$RUNCLIENT_LOG" 2>/dev/null >&2 || true
  tail -n 40 "$LAUNCH_LOG" 2>/dev/null >&2 || true
  exit 1
}

mkdir -p "$OUTDIR" "$METADIR"

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
  "profile": "ui_entities_oracle",
  "chat": false,
  "hide_gui": false,
  "strip": {
    "menus": true,
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
rm -rf "$ENVDIR/Minecraft/run/saves/qrl_${SEED}" \
       "$ENVDIR/Minecraft/run/saves/qrl_${SEED}_flat" 2>/dev/null || true

MALMO_JAR="$ENVDIR/Minecraft/build/libs/MalmoMod-0.37.0.jar"
# Never install a duplicate standalone qrl_bridge.jar into mods: runClient
# already loads the project MalmoMod (FMLCorePluginContainsFMLMod). A second
# jar re-registers qrl/qlook/malmomod and Malmo preInit fails schema validation
# (bridge omits schemas.index / *.xsd).
rm -f "$ENVDIR/Minecraft/build/libs/qrl_bridge.jar" \
      "$ENVDIR/Minecraft/run/mods/qrl_bridge.jar" \
      "$ENVDIR/Minecraft/.minecraft/mods/qrl_bridge.jar" \
      "$ENVDIR/Minecraft/.minecraftserver/mods/qrl_bridge.jar"
# runClient also scans run/mods; a second MalmoMod there duplicates the project.
rm -f "$ENVDIR/Minecraft/run/mods/MalmoMod-0.37.0.jar" \
      "$ENVDIR/Minecraft/run/mods/MalmoMod"*.jar 2>/dev/null || true

jar_has_schemas() {
  local j="$1"
  [ -f "$j" ] || return 1
  jar tf "$j" | grep -q '^schemas\.index$' || return 1
  for xsd in Types.xsd Mission.xsd MissionInit.xsd MissionHandlers.xsd MissionEnded.xsd; do
    jar tf "$j" | grep -q "^${xsd}$" || return 1
  done
  # entity_pin is required for this capture path
  local tmp
  tmp="$(mktemp -d)"
  ( cd "$tmp" && jar xf "$j" qrl/Recorder.class 2>/dev/null ) || { rm -rf "$tmp"; return 1; }
  strings "$tmp/qrl/Recorder.class" 2>/dev/null | grep -q 'entity_pin' || { rm -rf "$tmp"; return 1; }
  rm -rf "$tmp"
  return 0
}

if [ "${SKIP_JAR_BUILD:-0}" = 1 ] && jar_has_schemas "$MALMO_JAR"; then
  log "SKIP_JAR_BUILD=1 — reusing $MALMO_JAR (schemas + entity_pin present)"
elif jar_has_schemas "$MALMO_JAR" \
    && [ ! "$ENVDIR/Minecraft/src/main/java/qrl/Recorder.java" -nt "$MALMO_JAR" ] \
    && [ ! "$ENVDIR/Minecraft/src/main/resources/schemas.index" -nt "$MALMO_JAR" ]; then
  log "reusing up-to-date $MALMO_JAR (no source newer than jar)"
else
  log "building MalmoMod jar (entity_pin + frame rerender; includes schemas)..."
  (
    cd "$ENVDIR/Minecraft" || exit 1
    export JAVA_HOME
    # --no-daemon: avoid extra Gradle daemon under memory pressure (exit 137).
    ./gradlew -g run/gradle --no-daemon classes processResources jar -x test --offline 2>/dev/null \
      || ./gradlew -g run/gradle --no-daemon classes processResources jar -x test
  ) || fail "gradle jar failed"
fi
[ -f "$MALMO_JAR" ] || fail "missing $MALMO_JAR"
jar_has_schemas "$MALMO_JAR" || fail "MalmoMod jar missing schemas/entity_pin"
# Guard: no bridge left in any mods dir we own.
if find "$ENVDIR/Minecraft/run/mods" "$ENVDIR/Minecraft/.minecraft/mods" \
        "$ENVDIR/Minecraft/.minecraftserver/mods" \
        -name 'qrl_bridge.jar' 2>/dev/null | grep -q .; then
  fail "qrl_bridge.jar still present under mods (duplicate mod install)"
fi
log "MalmoMod jar ok (schemas.index + xsd + entity_pin); no qrl_bridge.jar in mods"

# Truncate prior runclient noise so fail-fast greps only see this launch.
: >"$RUNCLIENT_LOG" 2>/dev/null || true
: >"$LAUNCH_LOG" 2>/dev/null || true

log "launching headless game (JAVA_HOME=$JAVA_HOME)..."
( cd "$ENVDIR" && setsid nohup bash start_vnc_client.sh >"$LAUNCH_LOG" 2>&1 </dev/null 9>&- & )

log "waiting for qrl bridge on :25575 (up to 420s)..."
listened=0
for i in $(seq 1 420); do
  if uv run --no-project python -c 'import socket,sys
s=socket.socket(); s.settimeout(1)
try:
  s.connect(("127.0.0.1",25575)); s.close()
except Exception:
  sys.exit(1)' 2>/dev/null; then
    listened=1
    break
  fi
  # Fail fast if THIS launch already crashed (schema / mod duplicate).
  # Only scan the last 80 lines so older sessions cannot false-trigger.
  if [ -f "$RUNCLIENT_LOG" ] && tail -n 80 "$RUNCLIENT_LOG" 2>/dev/null \
      | grep -qE 'incorrectly built|Duplicate Mods|BUILD FAILED|Game crashed|LoaderExceptionModCrash'; then
    fail "client died before qrl listen (see $RUNCLIENT_LOG)"
  fi
  sleep 1
done
[ "$listened" = 1 ] || fail "qrl bridge never accepted a connection within 420s"
log "bridge up — verifying qrl server log..."
if ! grep -qE '\[qrl\].*listening on 127\.0\.0\.1:25575' "$RUNCLIENT_LOG" 2>/dev/null; then
  # socket is up; log line may still be buffering — soft check
  log "WARN: no '[qrl] listening' line yet; socket accepted — continuing"
else
  log "qrl log ok: listening on 127.0.0.1:25575"
fi
if tail -n 120 "$RUNCLIENT_LOG" 2>/dev/null | grep -qE 'incorrectly built|LoaderExceptionModCrash|Duplicate Mods'; then
  fail "qrl/Malmo errors in runclient.log after listen"
fi

log "running capture driver..."
cd "$ENVDIR" || fail "cd $ENVDIR"
DRIVER_ARGS=(--out "$OUTDIR" --seed "$SEED")
# Preserve existing non-empty goldens unless FORCE_RECAPTURE=1 (full overwrite).
if [ "${FORCE_RECAPTURE:-0}" != 1 ]; then
  DRIVER_ARGS+=(--skip-valid)
fi
if [ -n "${ONLY_STATES:-}" ]; then
  # shellcheck disable=SC2206
  ONLY_ARR=($ONLY_STATES)
  DRIVER_ARGS+=(--only "${ONLY_ARR[@]}")
fi
# pillow/numpy optional in driver (ground probe); required for post validate.
uv run --no-project --with pillow --with numpy \
  python "$SCRIPT_DIR/capture_ui_entities_driver.py" \
  "${DRIVER_ARGS[@]}" \
  || fail "capture driver failed"

REQUIRED=(
  slime_size1 slime_size2 slime_size4 slime_squish
  magma_size1 magma_size2 magma_size4 magma_squish
  dragon_death_50 dragon_death_100 dragon_death_190
  dig_stone dig_grass
  fireball_small fireball_dragon xp_orb
)
missing=0
for id in "${REQUIRED[@]}"; do
  for ab in a b; do
    f="$OUTDIR/${id}_${ab}.png"
    if [ ! -s "$f" ]; then
      echo "[capture_ui_entities] missing $f" >&2
      missing=1
    fi
  done
done
[ "$missing" = 0 ] || fail "one or more goldens missing"

log "validating non-vacuous presence + A/B stability..."
uv run --no-project --with pillow --with numpy \
  python "$SCRIPT_DIR/validate_ui_entities_goldens.py" \
    --goldens "$OUTDIR" \
    --require-meta \
    --json-out "$METADIR/validate_report.json" \
  || fail "golden validation failed (empty sky or unstable A/B) — not a commit baseline"

log "done: $(ls "$OUTDIR"/*_a.png 2>/dev/null | wc -l) states x2 under $OUTDIR"
