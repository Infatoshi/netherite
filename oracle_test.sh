#!/usr/bin/env bash
#
# Oracle end-to-end verification test.
#
# Runs record -> replay -> validate cycle using the test harness.
# The test harness creates a headless bot, scripts actions, exports state,
# and auto-shuts down the server after the configured tick count.
#
# Usage:
#   ./oracle_test.sh [ticks]
#
# Default: 200 ticks (~10 seconds of game time)
#
# Prerequisites:
#   - JDK 8 (temurin-8) at /Library/Java/JavaVirtualMachines/temurin-8.jdk/
#   - eula.txt accepted in forge-workspace/run/
#   - server.properties with seed=42, online-mode=false
#

set -euo pipefail

JAVA_HOME="${JAVA_HOME:-/Library/Java/JavaVirtualMachines/temurin-8.jdk/Contents/Home}"
export JAVA_HOME

TICKS="${1:-200}"
ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
FORGE_DIR="$ROOT_DIR/forge-workspace"
RUN_DIR="$FORGE_DIR/run"
WORLD_DIR="$RUN_DIR/world"
FORGESRC_JAR="$HOME/.gradle/caches/minecraft/net/minecraftforge/forge/1.7.10-10.13.4.1614-1.7.10/forgeSrc-1.7.10-10.13.4.1614-1.7.10.jar"

echo "=== Oracle E2E Test ==="
echo "Ticks: $TICKS"
echo "Java:  $JAVA_HOME"
echo ""

# ------------------------------------------------------------------
# Step 0: Compile modified mc-src and update forgeSrc jar
# ------------------------------------------------------------------
echo "[0/5] Compiling modified mc-src..."

if [ ! -f "$FORGESRC_JAR" ]; then
    echo "ERROR: forgeSrc jar not found at $FORGESRC_JAR"
    echo "Run: cd forge-workspace && ./gradlew setupDecompWorkspace --no-daemon"
    exit 1
fi

RECOMP_CLS="$FORGE_DIR/build/tmp/recompCls"
LOG4J_API="$(find "$HOME/.gradle/caches/modules-2" -name 'log4j-api-2.0-beta9.jar' 2>/dev/null | head -1)"
LOG4J_CORE="$(find "$HOME/.gradle/caches/modules-2" -name 'log4j-core-2.0-beta9.jar' 2>/dev/null | head -1)"
GUAVA="$(find "$HOME/.gradle/caches/modules-2" -name 'guava-17.0.jar' 2>/dev/null | head -1)"
NETTY="$(find "$HOME/.gradle/caches/modules-2" -name 'netty-all-4.0.10.Final.jar' 2>/dev/null | head -1)"
COMMONS_LANG="$(find "$HOME/.gradle/caches/modules-2" -name 'commons-lang3-3.3.2.jar' 2>/dev/null | head -1)"
AUTHLIB="$(find "$HOME/.gradle/caches/modules-2" -name 'authlib-*.jar' -not -name '*-sources*' 2>/dev/null | head -1)"
GSON="$(find "$HOME/.gradle/caches/modules-2" -name 'gson-*.jar' -not -name '*-sources*' -not -name '*-javadoc*' 2>/dev/null | head -1)"

CP="$LOG4J_API:$LOG4J_CORE:$FORGESRC_JAR:$RECOMP_CLS:$GUAVA:$NETTY:$COMMONS_LANG:$AUTHLIB:$GSON"

BUILD_DIR="/tmp/oracle_build_$$"
rm -rf "$BUILD_DIR" && mkdir -p "$BUILD_DIR"

"$JAVA_HOME/bin/javac" -cp "$CP" -d "$BUILD_DIR" -sourcepath "$ROOT_DIR/mc-src" \
    "$ROOT_DIR/mc-src/net/minecraft/oracle/OracleTestHarness.java" \
    "$ROOT_DIR/mc-src/net/minecraft/server/MinecraftServer.java" \
    "$ROOT_DIR/mc-src/net/minecraft/oracle/OracleReplay.java" \
    "$ROOT_DIR/mc-src/net/minecraft/oracle/OracleStateExporter.java" \
    "$ROOT_DIR/mc-src/net/minecraft/oracle/OracleRecorder.java" \
    "$ROOT_DIR/mc-src/net/minecraft/oracle/OracleAction.java" \
    "$ROOT_DIR/mc-src/net/minecraft/oracle/OracleValidator.java" \
    "$ROOT_DIR/mc-src/net/minecraft/network/NetworkManager.java" \
    "$ROOT_DIR/mc-src/net/minecraft/network/NetHandlerPlayServer.java" \
    "$ROOT_DIR/mc-src/net/minecraft/entity/Entity.java" \
    "$ROOT_DIR/mc-src/net/minecraft/world/Explosion.java" \
    "$ROOT_DIR/mc-src/cpw/mods/fml/common/network/FMLOutboundHandler.java" \
    2>&1 | grep -v "^Note:" || true

CLASS_COUNT=$(find "$BUILD_DIR" -name "*.class" -type f | wc -l | tr -d ' ')
echo "  Compiled $CLASS_COUNT classes"

cd "$BUILD_DIR"
"$JAVA_HOME/bin/jar" uf "$FORGESRC_JAR" $(find . -name "*.class" -type f | sed 's|^\./||')
echo "  forgeSrc jar updated."
cd "$ROOT_DIR"
rm -rf "$BUILD_DIR"

# ------------------------------------------------------------------
# Step 1: Clean world for fresh test
# ------------------------------------------------------------------
echo ""
echo "[1/5] Cleaning world directory for fresh test..."
if [ -d "$WORLD_DIR" ]; then
    rm -rf "$WORLD_DIR"
    echo "  Deleted existing world."
fi
rm -rf "$RUN_DIR/crash-reports"

# Save recording between phases (it lives inside world dir which gets deleted)
RECORDING_STASH="$RUN_DIR/oracle_recording_stash.nrec"
SNAPSHOT_STASH="$RUN_DIR/oracle_record_snapshot_stash.nsta"
rm -f "$RECORDING_STASH" "$SNAPSHOT_STASH" 2>/dev/null || true

# ------------------------------------------------------------------
# Step 2: Record phase
# ------------------------------------------------------------------
echo ""
echo "[2/5] RECORD phase: starting server with headless bot for $TICKS ticks..."
cd "$FORGE_DIR"
ORACLE_TEST=record ORACLE_TEST_TICKS="$TICKS" \
    "$FORGE_DIR/gradlew" runServer --no-daemon \
    2>&1 | tee "$ROOT_DIR/oracle_test_record.log" || true

# Find and stash the recording + snapshot before deleting world
RECORDING="$WORLD_DIR/oracle_recording.nrec"
if [ ! -f "$RECORDING" ]; then
    echo "ERROR: Recording file not created at $RECORDING"
    echo "Check oracle_test_record.log for details."
    exit 1
fi
cp "$RECORDING" "$RECORDING_STASH"
echo "  Recording: $(wc -c < "$RECORDING" | tr -d ' ') bytes"

RECORD_SNAPSHOT=$(find "$WORLD_DIR" -name "oracle_record_dim0_*.nsta" 2>/dev/null | head -1)
if [ -z "$RECORD_SNAPSHOT" ]; then
    echo "ERROR: Record snapshot not created."
    echo "Check oracle_test_record.log for details."
    exit 1
fi
cp "$RECORD_SNAPSHOT" "$SNAPSHOT_STASH"
echo "  Record snapshot: $(basename "$RECORD_SNAPSHOT") ($(wc -c < "$RECORD_SNAPSHOT" | tr -d ' ') bytes)"

# ------------------------------------------------------------------
# Step 3: Replay phase
# ------------------------------------------------------------------
echo ""
echo "[3/5] REPLAY phase: starting fresh server, replaying recording for $TICKS ticks..."

# Delete world to regenerate from same seed, but pre-create dir with recording
rm -rf "$WORLD_DIR"
mkdir -p "$WORLD_DIR"
cp "$RECORDING_STASH" "$WORLD_DIR/oracle_recording.nrec"

cd "$FORGE_DIR"
ORACLE_TEST=replay ORACLE_TEST_TICKS="$TICKS" \
    "$FORGE_DIR/gradlew" runServer --no-daemon \
    2>&1 | tee "$ROOT_DIR/oracle_test_replay.log" || true

REPLAY_SNAPSHOT=$(find "$WORLD_DIR" -name "oracle_replay_dim0_*.nsta" 2>/dev/null | head -1)
if [ -z "$REPLAY_SNAPSHOT" ]; then
    echo "ERROR: Replay snapshot not created."
    echo "Check oracle_test_replay.log for details."
    exit 1
fi
echo "  Replay snapshot: $(basename "$REPLAY_SNAPSHOT") ($(wc -c < "$REPLAY_SNAPSHOT" | tr -d ' ') bytes)"

# ------------------------------------------------------------------
# Step 4: Validate (in-process using server's validate mode)
# ------------------------------------------------------------------
echo ""
echo "[4/5] VALIDATE: comparing record vs replay snapshots..."

# Copy record snapshot into world dir for the validate harness
cp "$SNAPSHOT_STASH" "$WORLD_DIR/oracle_record_dim0_tick$(($TICKS + 1)).nsta"

cd "$FORGE_DIR"
ORACLE_TEST=validate ORACLE_TEST_TICKS="$TICKS" \
    "$FORGE_DIR/gradlew" runServer --no-daemon \
    2>&1 | tee "$ROOT_DIR/oracle_test_validate.log" || true

# Check validation result from log
if grep -q "VALIDATION PASSED" "$ROOT_DIR/oracle_test_validate.log"; then
    RESULT=0
elif grep -q "VALIDATION FAILED" "$ROOT_DIR/oracle_test_validate.log"; then
    RESULT=1
else
    echo "WARNING: Could not determine validation result from log."
    RESULT=2
fi

# ------------------------------------------------------------------
# Step 5: Report
# ------------------------------------------------------------------
echo ""
echo "[5/5] Results"
echo "============================================"
if [ $RESULT -eq 0 ]; then
    echo "  ORACLE TEST: PASS"
    echo "  Record and replay snapshots match."
elif [ $RESULT -eq 1 ]; then
    echo "  ORACLE TEST: FAIL"
    echo "  Snapshots diverged. See oracle_test_validate.log."
    FAIL_LINE=$(grep "VALIDATION FAILED" "$ROOT_DIR/oracle_test_validate.log" | head -1)
    FIRST_LINE=$(grep "First:" "$ROOT_DIR/oracle_test_validate.log" | head -1)
    [ -n "$FAIL_LINE" ] && echo "  $FAIL_LINE"
    [ -n "$FIRST_LINE" ] && echo "  $FIRST_LINE"
else
    echo "  ORACLE TEST: UNKNOWN"
    echo "  Could not determine result. Check logs."
fi
echo "============================================"
echo ""
echo "Artifacts:"
echo "  Logs:            oracle_test_record.log, oracle_test_replay.log, oracle_test_validate.log"
echo "  Record snapshot: $SNAPSHOT_STASH"
echo "  Replay snapshot: $REPLAY_SNAPSHOT"
echo "  Recording:       $RECORDING_STASH"
exit $RESULT
