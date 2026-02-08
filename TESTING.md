# Oracle Verification Testing

## Quick Start (Automated)

```bash
./oracle_test.sh        # Run with 200 ticks (default)
./oracle_test.sh 50     # Run with 50 ticks (faster, for smoke tests)
```

The script runs the full cycle: compile -> record -> replay -> validate.

## Current Status

The test harness works end-to-end: record, replay, and validate all complete
successfully. **Validation currently reports divergences** (~258 at 50 ticks) due
to non-deterministic entity/mob behavior. This is expected until the Entity.rand
seeding fix propagates through a full recompile of all entity classes (not just
Entity.java itself). The validation infrastructure correctly identifies and
reports these divergences.

## Prerequisites

1. **JDK 8** (Temurin recommended):
   ```bash
   export JAVA_HOME=/Library/Java/JavaVirtualMachines/temurin-8.jdk/Contents/Home
   ```

2. **ForgeGradle workspace set up** (one-time):
   ```bash
   cd forge-workspace
   JAVA_HOME=... ./gradlew setupDecompWorkspace --no-daemon
   ```

3. **EULA accepted**:
   ```bash
   echo "eula=true" > forge-workspace/run/eula.txt
   ```

4. **server.properties** configured (should already be):
   - `level-seed=42`
   - `online-mode=false`
   - `player-idle-timeout=0`

## How It Works

### Phase 1: Record

The server starts with `ORACLE_TEST=record`. The `OracleTestHarness` creates a
headless bot (fake `EntityPlayerMP` with a dummy `NetworkManager`) and scripts a
sequence of actions:

- Walk forward (position updates every tick)
- Change held item slot (ticks 40, 80)
- Swing arm (ticks 60, 120)
- Start/stop sprinting (ticks 100, 160)

The oracle recording hooks in `NetHandlerPlayServer` capture all actions to
`oracle_recording.nrec`. After N ticks, `OracleStateExporter` dumps the overworld
state (chunks + entities + player) to `oracle_record_dim0_tickN.nsta`. Server shuts
down automatically.

### Phase 2: Replay

The world is deleted and regenerated from seed 42. The server starts with
`ORACLE_TEST=replay`. The recording file must be present in the world directory.
The harness loads the `.nrec` recording and `OracleReplay` creates a new headless
bot, then injects the recorded actions at the same absolute tick numbers.

After the same N ticks, another state snapshot is exported to
`oracle_replay_dim0_tickN.nsta`. Server shuts down.

### Phase 3: Validate

`OracleValidator` compares the two `.nsta` snapshots:

- **Chunks**: byte-for-byte comparison of block IDs and metadata
- **Entities**: type match, position within epsilon (1e-6), health match
- **Player**: position, motion, health, food, saturation, inventory, dimension

Reports PASS if identical, FAIL with first divergence details.

## Manual Test Procedure

### 1. Compile and update forgeSrc

The build process compiles modified `mc-src/` files against log4j-2.0-beta9 and
updates the forgeSrc jar. The `oracle_test.sh` script handles this automatically.
To do it manually:

```bash
JAVA_HOME=/Library/Java/JavaVirtualMachines/temurin-8.jdk/Contents/Home
FORGESRC="$HOME/.gradle/caches/minecraft/net/minecraftforge/forge/1.7.10-10.13.4.1614-1.7.10/forgeSrc-1.7.10-10.13.4.1614-1.7.10.jar"
RECOMP="forge-workspace/build/tmp/recompCls"
LOG4J="$(find ~/.gradle/caches/modules-2 -name 'log4j-api-2.0-beta9.jar' | head -1)"
LOG4J_CORE="$(find ~/.gradle/caches/modules-2 -name 'log4j-core-2.0-beta9.jar' | head -1)"
GUAVA="$(find ~/.gradle/caches/modules-2 -name 'guava-17.0.jar' | head -1)"
NETTY="$(find ~/.gradle/caches/modules-2 -name 'netty-all-4.0.10.Final.jar' | head -1)"
COMMONS_LANG="$(find ~/.gradle/caches/modules-2 -name 'commons-lang3-3.3.2.jar' | head -1)"
AUTHLIB="$(find ~/.gradle/caches/modules-2 -name 'authlib-*.jar' -not -name '*-sources*' | head -1)"
GSON="$(find ~/.gradle/caches/modules-2 -name 'gson-*.jar' -not -name '*-sources*' -not -name '*-javadoc*' | head -1)"

CP="$LOG4J:$LOG4J_CORE:$FORGESRC:$RECOMP:$GUAVA:$NETTY:$COMMONS_LANG:$AUTHLIB:$GSON"

mkdir -p /tmp/oracle_build
$JAVA_HOME/bin/javac -cp "$CP" -d /tmp/oracle_build -sourcepath mc-src \
    mc-src/net/minecraft/oracle/*.java \
    mc-src/net/minecraft/server/MinecraftServer.java \
    mc-src/net/minecraft/network/NetworkManager.java \
    mc-src/net/minecraft/network/NetHandlerPlayServer.java \
    mc-src/net/minecraft/entity/Entity.java \
    mc-src/net/minecraft/world/Explosion.java \
    mc-src/cpw/mods/fml/common/network/FMLOutboundHandler.java

cd /tmp/oracle_build
$JAVA_HOME/bin/jar uf "$FORGESRC" $(find . -name "*.class" -type f | sed 's|^\./||')
```

**Important**: Must compile against log4j-2.0-beta9, NOT a newer version. The
runtime uses beta9 which lacks some overloads present in newer versions, causing
NoSuchMethodError at runtime.

### 2. Record

```bash
rm -rf forge-workspace/run/world
cd forge-workspace
ORACLE_TEST=record ORACLE_TEST_TICKS=50 ./gradlew runServer --no-daemon
```

Verify output includes:
```
[Oracle Test] RECORD mode: bot created at tick 1, running 50 ticks
[Oracle] Recording started: .../oracle_recording.nrec (seed=42, startTick=1)
[Oracle] Recording stopped: 50 actions written to .../oracle_recording.nrec
[Oracle Test] ORACLE_RECORD phase complete. Shutting down.
```

### 3. Replay

```bash
# Save recording, then set up fresh world with recording
cp forge-workspace/run/world/oracle_recording.nrec /tmp/
cp forge-workspace/run/world/oracle_record_dim0_*.nsta /tmp/oracle_record.nsta
rm -rf forge-workspace/run/world
mkdir -p forge-workspace/run/world
cp /tmp/oracle_recording.nrec forge-workspace/run/world/

cd forge-workspace
ORACLE_TEST=replay ORACLE_TEST_TICKS=50 ./gradlew runServer --no-daemon
```

### 4. Validate

```bash
# Copy record snapshot into world dir, then run validate mode
cp /tmp/oracle_record.nsta forge-workspace/run/world/oracle_record_dim0_tick51.nsta

cd forge-workspace
ORACLE_TEST=validate ./gradlew runServer --no-daemon
```

## Expected Divergences

Divergences are currently expected from:

1. **Mob positions/spawning**: Entity.rand is seeded per-entity but the recompCls
   (original Minecraft classes) still use unseeded Random for entities not
   recompiled from mc-src. Different mob positions cause different block
   interactions (trampled farmland, path changes, etc.).
2. **Entity count differences**: Different spawn timing means different entity
   counts in the snapshots.
3. **Metadata differences**: Block metadata changes caused by mob interactions
   (e.g., farmland moisture, door states near mobs).

Once all entity classes are recompiled with the seeded Random fix, these
divergences should reduce significantly.

## Troubleshooting

**Server doesn't start**: Check `forge-workspace/run/eula.txt` contains `eula=true`

**NoSuchMethodError on Logger**: You compiled against the wrong log4j version.
The runtime uses log4j-2.0-beta9. Ensure `log4j-api-2.0-beta9.jar` is FIRST on
the javac classpath.

**Recording not created**: The test harness starts recording explicitly. Check
logs for errors during bot creation.

**Replay "Recording file not found"**: The recording file must be in the world
directory before the server finishes loading. Pre-create the world dir and copy
the recording before starting the server.

**Build: NullPointerException in FMLOutboundHandler**: The headless bot's dummy
NetworkManager has no Netty channel. The null-safe check in FMLOutboundHandler
handles this. If you see this NPE, the forgeSrc jar wasn't updated with the
FMLOutboundHandler fix.

## File Locations

| File | Location |
|------|----------|
| Recording | `forge-workspace/run/world/oracle_recording.nrec` |
| Record snapshot | `forge-workspace/run/world/oracle_record_dim0_tickN.nsta` |
| Replay snapshot | `forge-workspace/run/world/oracle_replay_dim0_tickN.nsta` |
| Record log | `oracle_test_record.log` |
| Replay log | `oracle_test_replay.log` |
| Validate log | `oracle_test_validate.log` |
| Test harness | `mc-src/net/minecraft/oracle/OracleTestHarness.java` |
| Validator | `mc-src/net/minecraft/oracle/OracleValidator.java` |
| FML null-safe fix | `mc-src/cpw/mods/fml/common/network/FMLOutboundHandler.java` |
