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
