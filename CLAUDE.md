# Netherite: Minecraft 1.7.10 Minimal Oracle

## Project Overview

A stripped-down Minecraft 1.7.10 implementation targeting the "beat the game" critical path. This serves as a ground truth oracle for validating a future C/CUDA physics engine (MegaColonel) that will run thousands of parallel game instances for reinforcement learning.

## CRITICAL: Implementation Approach

**THIS IS A SUBTRACTION PROJECT, NOT A GREENFIELD BUILD.**

Do NOT write Minecraft from scratch. The MCP-decompiled 1.7.10 source already has working, correct implementations of physics, collision, entity AI, and world generation. We inherit that correctness by deleting what we don't need.

### Phase 1: MCP Environment Setup
1. Download MCP 9.08 (Mod Coder Pack for 1.7.10)
2. Obtain vanilla Minecraft 1.7.10 client and server jars
3. Run MCP decompilation process
4. Verify decompiled source compiles and runs

### Phase 2: Systematic Deletion
Remove systems we don't need, one at a time, verifying the game still works after each removal:

**Deletion Order (safest first):**
1. Decorative blocks (stained glass, flowers, carpets, etc.)
2. Command blocks, jukeboxes, note blocks
3. Brewing system (brewing stand, potions, cauldron)
4. Enchanting system (enchanting table, anvil, bookshelves logic)
5. Beacon system
6. Redstone system (wire, torches, repeaters, comparators, pistons, hoppers, droppers, dispensers)
7. Excluded entities (villagers, horses, wolves, ocelots, witch, slime, cave spider, iron golem, snow golem, wither)

After each deletion batch, run the game and verify:
- Game launches
- World generates
- Player can move, mine, craft
- Portals work
- Target mobs still spawn

### Phase 3: Oracle Instrumentation
Add recording/replay infrastructure to the stripped codebase:
1. Action recording (capture all player inputs with tick timestamps)
2. State export (dump world state in flat format)
3. Action replay (deterministic playback)
4. Validation (compare replayed state to original)

### Phase 4: Verification
1. Human plays through and beats the game on stripped version
2. Record the playthrough
3. Replay and verify byte-identical world state
4. Run oracle test suite

## Source Material

- **Base version**: Minecraft 1.7.10
- **Mappings**: MCP 9.08 (most thoroughly documented version)
- **MCP Download**: http://www.modcoderpack.com/ (or archived mirrors)
- **Reference**: `/mcp_1.7.10/` contains downloaded mappings and documentation

## Feature Scope

### KEEP (Critical Path)

**Biomes & Terrain**
- All biomes including oceans
- Caves, ravines
- Full procedural worldgen with fixed seed support

**Structures**
- Nether fortress (required for blaze)
- Stronghold (required for end portal)
- Dungeon, mineshaft, desert temple, jungle temple (incidental)

**Blocks (~80 types)**
- Stone, dirt, grass, sand, gravel, clay, obsidian
- All ores (coal, iron, gold, diamond, redstone, lapis, emerald)
- Crafting table, furnace, chest, ender chest
- Nether portal, end portal frame, end portal, end stone
- Nether brick, nether rack, soul sand, glowstone
- Mob spawner, ladder, fence, door, torch, TNT
- Water, lava, ice, snow, farmland, crops
- Bed, anvil, enchanting table (kept for structure gen)

**Entities (Critical Path)**
- Player, zombies, skeletons, creepers, spiders, endermen
- Blazes, ghasts, zombie pigmen, silverfish
- Ender dragon, ender crystals
- Items, XP orbs, falling blocks, TNT primed
- Arrows, snowballs, ender pearls, eyes of ender

**Systems**
- Crafting (all recipes for critical path items)
- Smelting (furnace recipes)
- Combat (melee, ranged, armor)
- Hunger/food
- Mob spawning (natural + spawner)
- Portal mechanics (nether + end)
- World generation (all biomes, structures)

### DELETE (Non-Critical)

- Decorative blocks, stained glass, flowers, carpets
- Command blocks, jukeboxes, note blocks
- Brewing system, potions, cauldron
- Beacon system
- Redstone system (wire, repeaters, pistons, hoppers, etc.)
- Villagers, horses, wolves, ocelots, witch, iron/snow golems, wither

## Current Status

**Phase 1**: COMPLETE - ForgeGradle 1.2 dev environment with MCP-decompiled 1.7.10 source
**Phase 2**: COMPLETE - Stripped server boots, loads all 3 dimensions
**Phase 3**: IN PROGRESS - Oracle instrumentation

### Phase 3 Components

| Component | Status |
|-----------|--------|
| Action recording (OracleRecorder) | DONE |
| State export (OracleStateExporter) | DONE |
| Action replay (OracleReplay) | DONE |
| Validation (OracleValidator) | DONE |
| Determinism fixes (Entity.rand, Explosion.explosionRNG, etc.) | DONE |
| Checkpoint test system (10 checkpoints) | DONE |
| Vanilla client connection (FML handshake bypass) | DONE |
| Human player checkpoint mode | DONE |
| Bot automation for checkpoints | IN PROGRESS |

### Checkpoint Test System

10 checkpoints covering the critical path:

| Checkpoint | Auto-test | Status |
|-----------|-----------|--------|
| water_bucket | PASS (tick 1) | Automated |
| fall_damage | PASS (tick 22) | Automated |
| mob_spawning | PASS (tick 2218) | Automated |
| nether_portal | Needs human | Setup verified |
| nether_fortress | Needs human | Setup verified |
| enderman_hunt | Needs human | Setup verified |
| stronghold | Needs human | Setup verified |
| crafting | Needs human | Setup verified |
| dragon_full | Needs human | Setup verified |
| dragon_1hp | Needs human | **Playtested -- credits reached** |

### Running Checkpoints

```bash
cd forge-workspace

# Auto-test (headless bot, exits with PASS/FAIL):
JAVA_HOME=/Library/Java/JavaVirtualMachines/temurin-8.jdk/Contents/Home \
  ORACLE_CHECKPOINT=water_bucket ORACLE_AUTOTEST=true \
  ./gradlew runServer --no-daemon

# Human playtest (connect vanilla 1.7.10 client to localhost:25570):
JAVA_HOME=/Library/Java/JavaVirtualMachines/temurin-8.jdk/Contents/Home \
  ORACLE_CHECKPOINT=dragon_1hp \
  ./gradlew runServer --no-daemon

# Run all 3 auto-testable checkpoints:
for cp in water_bucket fall_damage mob_spawning; do
  rm -rf run/world
  JAVA_HOME=/Library/Java/JavaVirtualMachines/temurin-8.jdk/Contents/Home \
    ORACLE_CHECKPOINT=$cp ORACLE_AUTOTEST=true \
    ./gradlew runServer --no-daemon
done
```

## Build System

### Manual Compilation (mc-src changes)

ForgeGradle only compiles mod code, not mc-src. Manual javac required:

```bash
FORGESRC="$HOME/.gradle/caches/minecraft/net/minecraftforge/forge/1.7.10-10.13.4.1614-1.7.10/forgeSrc-1.7.10-10.13.4.1614-1.7.10.jar"
LOG4J="$HOME/.gradle/caches/modules-2/files-2.1/org.apache.logging.log4j/log4j-api/2.0-beta9/1dd66e68cccd907880229f9e2de1314bd13ff785/log4j-api-2.0-beta9.jar"
AUTHLIB="$HOME/.gradle/caches/modules-2/files-2.1/com.mojang/authlib/1.5.21/aefba0d5b53fbcb70860bc8046ab95d5854c07a5/authlib-1.5.21.jar"
GUAVA="$HOME/.gradle/caches/modules-2/files-2.1/com.google.guava/guava/17.0/9c6ef172e8de35fd8d4d8783e4571f4b8af5a6a2/guava-17.0.jar"
NETTY="$HOME/.gradle/caches/modules-2/files-2.1/io.netty/netty-all/4.0.10.Final/6e5b1c1b650e20e80c8b00cb2b3000b1e56f8a36/netty-all-4.0.10.Final.jar"

mkdir -p /tmp/build
javac -cp "$LOG4J:$FORGESRC:forge-workspace/build/classes/main:$GUAVA:$AUTHLIB:$NETTY" \
  -d /tmp/build -sourcepath mc-src mc-src/path/to/File.java
cd /tmp/build && jar uf "$FORGESRC" $(find . -name "*.class" | sed 's|^\./||')
```

**CRITICAL**: log4j-2.0-beta9 must be FIRST on classpath. Use string concat in logger calls, NOT `{}` format patterns.

### Server Config

- Port: 25570 (`forge-workspace/run/server.properties`)
- online-mode: false (vanilla clients accepted)
- Seed: 42
- Max players: 2

## Architecture

```
mc-src/                          # MCP-decompiled MC 1.7.10 source (modified)
  net/minecraft/oracle/          # Oracle instrumentation package
    OracleRecorder.java          # Binary action recording (.nrec format)
    OracleStateExporter.java     # Chunk/entity/player state export (.nsta format)
    OracleReplay.java            # Recording playback with headless bot
    OracleValidator.java         # Snapshot comparison
    OracleAction.java            # Action type IDs and payload builders
    CheckpointInitializer.java   # Checkpoint test system (10 scenarios)
    TestCheckpoint.java          # Checkpoint enum definitions
  net/minecraft/server/          # Server core (MinecraftServer tick hooks)
  net/minecraft/network/         # NetHandlerPlayServer (recording hooks)
  cpw/mods/fml/.../NetworkDispatcher.java  # Vanilla client acceptance
forge-workspace/                 # ForgeGradle project
  build.gradle                   # Env var forwarding for ORACLE_* props
  run/                           # Server runtime (world, logs, properties)
```

## Key Technical Notes

- EntityPlayerMP.onUpdate() does NOT call super.onUpdate() -- server-side physics don't run for players (client-authoritative movement)
- `setBlock` flag 2 = client notify only; flag 3 = block update + client notify. Fluid flow requires flag 3
- `EntityPlayer.fall()` is protected -- use `attackEntityFrom(DamageSource.fall, damage)` from external code
- `setCurrentItemOrArmor(slot, stack)`: slot 0=held, 1=boots, 2=leggings, 3=chestplate, 4=helmet
- End island surface at (0,0) is ~y62-65. Spawn platforms must be at y=75+ to avoid suffocation
- Mob spawn exclusion: mobs can't spawn within 24 blocks of any player
- `transferPlayerToDimension` can crash if called during world init -- use 60-tick delay after player join
