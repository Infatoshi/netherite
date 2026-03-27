# Netherite v2: Fast Minecraft RL Environment

## Goal

Fully Python-controlled Minecraft RL environment. Every game setting configurable from Python: resolution, render distance, game rules, graphics quality, JVM count, instances per JVM. Zero manual GUI interaction -- boot straight into a world. Performance depends on these knobs (resolution, render distance, graphics level, instance count) and is explored empirically, not targeted at a fixed multiplier.

Uses MC 1.20.1 + Sodium + Fabric + a tiny custom mod. No custom rendering code. MC's OpenGL renderer handles everything correctly (mobs, entities, water, particles, sky, HUD). We capture frames via PBO async readback and pipe them to Python through shared memory.

## Why Not Custom Rendering

Previous attempt: built a CUDA software rasterizer that replicated MC's OpenGL output. Achieved 93% pixel-exact match for static blocks, but hit unbounded rendering bugs: missing mobs/entities, water artifacts, x-ray vision, no particles/sky/HUD. Every fix revealed new issues. MC has hundreds of rendering features and reimplementing them all is a losing game.

The insight: MC's own renderer already handles everything. The bottleneck in MineRL isn't the GPU (it's mostly idle) -- it's the CPU-side overhead of thousands of GL draw calls per frame and synchronous frame readback. Sodium fixes the draw calls (batches everything into ~1-5 indirect draws). PBO async readback fixes the stall.

## Architecture

```
Python gym.Env (netherite_env.py)
    |
    | shmem: /tmp/netherite_obs_{id}_{A,B}    (pixels, ~1.6MB)
    | shmem: /tmp/netherite_state_{id}         (pos/health/inventory, ~64KB)
    | shmem: /tmp/netherite_action_{id}        (movement/camera/interact, 4KB)
    |
MC 1.20.1 Client (Java 17+, Fabric)
    ├── Sodium 0.5.x          -- rendering: one indirect draw for all chunks
    ├── Lithium 0.11.x        -- game logic: 30-40% faster ticking
    └── netherite-mod.jar      -- OUR MOD:
        ├── FrameGrabber.java      -- PBO double-buffered async readback -> shmem
        ├── ActionInjector.java    -- shmem -> player keyboard/mouse input
        ├── StateExporter.java     -- pos/health/inventory/entities -> shmem
        ├── WorldController.java   -- auto-create world, reset, teleport, seed control
        ├── NetheriteMod.java      -- entry point, registers tick handlers
        └── mixin/
            └── GameRendererMixin.java  -- hooks end-of-frame for PBO readback
```

## Tech Stack

| Component | Version | Source |
|---|---|---|
| Minecraft | 1.20.1 | Mojang |
| Fabric Loader | 0.16.14+ | fabricmc.net |
| Fabric API | 0.92.2+1.20.1 | fabricmc.net |
| Fabric Loom (gradle plugin) | 1.9-SNAPSHOT | fabricmc.net |
| Yarn Mappings | 1.20.1+build.10 | fabricmc.net |
| Sodium | mc1.20.1-0.5.11 | Modrinth maven (CaffeineMC, LGPL-3.0) |
| Lithium | mc1.20.1-0.11.2 | Modrinth maven (CaffeineMC, LGPL-3.0) |
| Java | 17+ (21 on anvil, whatever on macOS) | OpenJDK |
| Gradle | 8.12 | gradle.org |
| Python | 3.11+ | UV only |

## Shmem Protocol

All shmem uses memory-mapped files. On Linux: `/dev/shm/netherite_*`. On macOS: `/tmp/netherite_*`.

### Observation Buffer (pixels)
Path: `netherite_obs_{instance_id}_{A,B}` (double-buffered)
Size: 8MB per slot
```
Offset 0:  uint32 magic = 0x4E455432 ("NET2")
Offset 4:  uint32 frame_number
Offset 8:  uint32 data_size (W * H * 4)
Offset 12: uint32 ready_flag (written LAST)
Offset 16: uint8[W*H*4] RGBA pixels (GL bottom-up row order)
```
Writer (Java): clears ready_flag, writes pixels, sets ready_flag last.
Reader (Python): spins on ready_flag, reads data_size bytes.

### State Buffer
Path: `netherite_state_{instance_id}`
Size: 64KB
```
Offset 0:  uint32 magic = 0x4E455453 ("NETS")
Offset 4:  uint32 tick_number
Offset 8:  uint32 data_size
Offset 12: uint32 ready_flag
Offset 16: Player state:
  double x, y, z          (24 bytes)
  float yaw, pitch         (8 bytes)
  float health, max_health (8 bytes)
  int food_level           (4 bytes)
  float saturation         (4 bytes)
  int on_ground            (4 bytes)
  int in_water             (4 bytes)
Offset 72: Hotbar (9 slots):
  Per slot: int item_id (4) + int count (4) = 8 bytes each = 72 bytes
Offset 144: Nearby entities:
  int entity_count
  Per entity: int type_id (4) + double x,y,z (24) + float health (4) = 36 bytes
  Up to 32 entities = 1152 bytes max
```

### Action Buffer
Path: `netherite_action_{instance_id}`
Size: 4KB
```
Offset 0:  uint32 magic = 0x4E455441 ("NETA")
Offset 4:  uint32 tick_number
Offset 8:  uint32 data_size
Offset 12: uint32 ready_flag
Offset 16: Action payload:
  byte forward, back, left, right  (movement, 0 or 1)
  byte jump, sneak, sprint         (modifiers)
  byte attack, use                 (interact)
  byte camera_dx, camera_dy        (signed, -127 to 127)
```
Camera delta applied ONCE per new tick_number (frame-skip safe).
Movement keys held every tick (re-applied).

## Java Mod Implementation Details

### FrameGrabber.java (~120 lines)
- Creates 2 PBOs (GL_PIXEL_PACK_BUFFER) on init
- Each frame: kick async glReadPixels into PBO[N%2], map PBO[(N+1)%2] and copy to shmem
- Zero GPU stall -- one frame of latency (fine for RL)
- Triggered by GameRendererMixin @Inject at TAIL of GameRenderer.render()
- Uses LWJGL 3 GL calls directly (GL11, GL15, GL21 -- all on MC's classpath)
- Shmem via RandomAccessFile + FileChannel.map (MappedByteBuffer)
- Double-buffered shmem (A/B slots) so reader never sees partial writes

### ActionInjector.java (~100 lines)
- Maps shmem action buffer on init
- Each tick: read action, set KeyBinding states via KeyBinding.setKeyPressed()
- Camera rotation: player.setYaw() / player.setPitch()
- Frame-skip safe: track last tick_number, only apply camera delta once per new tick

### StateExporter.java (~120 lines)
- Maps shmem state buffer on init
- Each tick: write player pos/health/food, hotbar contents, nearby entities
- Entity scan: mc.world.getEntities() filtered by distance < 16 blocks, max 32
- Item IDs via Registries.ITEM.getRawId()
- Entity type IDs via Registries.ENTITY_TYPE.getRawId()

### WorldController.java (~100 lines)
- Detects title screen, auto-creates singleplayer world
- Fixed seed via -Dnetherite.seed=N system property
- GameRules: no daylight cycle, no weather cycle, no mob spawning (configurable)
- Reset support: delete world + recreate, or teleport to spawn
- Instance ID via -Dnetherite.instance=N

### NetheriteMod.java (~30 lines)
- Implements ClientModInitializer
- Registers ClientTickEvents.END_CLIENT_TICK -> ActionInjector.tick() + StateExporter.tick() + WorldController.tick()
- FrameGrabber triggered separately by mixin (needs to run in render thread, not tick thread)

### GameRendererMixin.java (~15 lines)
- @Mixin(GameRenderer.class)
- @Inject(method = "render", at = @At("TAIL"))
- Calls FrameGrabber.INSTANCE.onFrameReady()
- This runs AFTER the full frame is rendered (world + entities + HUD + chat)

## Python Gym Environment

### netherite_env.py
```python
class NetheriteEnv(gym.Env):
    observation_space = Dict({
        'pov': Box(0, 255, (H, W, 3), dtype=np.uint8),
        'inventory': Box(0, 64, (9, 2), dtype=np.int32),  # hotbar: item_id, count
        'health': Box(0, 20, (1,), dtype=np.float32),
        'position': Box(-1e6, 1e6, (3,), dtype=np.float64),
    })
    action_space = Dict({
        'forward': Discrete(2),
        'back': Discrete(2),
        'left': Discrete(2),
        'right': Discrete(2),
        'jump': Discrete(2),
        'sneak': Discrete(2),
        'sprint': Discrete(2),
        'attack': Discrete(2),
        'use': Discrete(2),
        'camera': Box(-180, 180, (2,), dtype=np.float32),  # delta yaw, pitch
    })
```

MineRL-compatible API. drop-in replacement.

### shmem_reader.py
- mmap the obs and state buffers
- Poll ready_flag, read pixels + state
- Flip Y (GL stores bottom-up)
- Convert RGBA -> RGB for observation

### shmem_writer.py
- mmap the action buffer
- Pack action dict into binary format
- Set ready_flag

## Build & Run

### Local Development (macOS)
```bash
cd ~/netherite
./gradlew build          # builds mod jar
./gradlew runClient      # launches MC with mod (you'll see the window)
```

### Headless Training (anvil, Linux)
```bash
# Start Xvfb for headless GL
Xvfb :99 -screen 0 854x480x24 &
export DISPLAY=:99

# Run with Sodium + Lithium
cd ~/netherite
./gradlew runClient -Dnetherite.instance=0 -Dnetherite.seed=12345

# Python training
uv run python train.py --num-envs 16
```

### Multi-Instance
Each instance gets unique:
- `-Dnetherite.instance=N` (shmem paths, save dirs)
- `-Dnetherite.seed=M` (world seed)
- Separate Gradle daemon or direct Java launch

## Performance Targets

| Metric | MineRL | Netherite v2 Target |
|---|---|---|
| Frame readback | ~5ms (sync glReadPixels) | ~0ms (PBO async) |
| Draw calls/frame | ~3000 (vanilla) | ~1-5 (Sodium indirect) |
| Server TPS | 20 | 20+ (Lithium) |
| Env step latency | ~50ms | ~16ms (60fps) |
| Throughput (B=1) | ~20 env/sec | ~60 env/sec |
| Throughput (B=16) | ~20 env/sec | ~200+ env/sec |
| Rendering correctness | 100% | 100% (same GL renderer) |

## What Carries Over From v1

These optimizations from the 1.8.9 CUDA rasterizer project apply here too:
1. Shmem double-buffer protocol (proven, zero-copy)
2. Frame-skip-safe action injection (held keys repeat, camera delta once)
3. Pipelined render || train architecture
4. Multi-instance parameterization
5. TickAccelerator concept (strip unnecessary server work)
6. Python gym.Env design (action/obs spaces, rewards)

## What's New vs v1

1. No custom rendering code (Sodium handles it)
2. PBO async readback instead of glReadPixels sync
3. Fabric instead of Forge (lighter, faster startup)
4. MC 1.20.1 instead of 1.8.9 (modern Java, better APIs)
5. Mixin API instead of reflection hacks
6. Works on macOS for development (no CUDA needed)

## File Structure

```
~/netherite/
├── SPEC.md                          # this file
├── CLAUDE.md                        # build/run instructions for Claude
├── build.gradle                     # Fabric Loom build
├── settings.gradle                  # plugin repos
├── gradle.properties                # version pins
├── gradle/wrapper/                  # Gradle wrapper
├── src/main/java/com/netherite/mod/
│   ├── NetheriteMod.java            # entry point
│   ├── FrameGrabber.java            # PBO async readback
│   ├── ActionInjector.java          # shmem -> player input
│   ├── StateExporter.java           # game state -> shmem
│   ├── WorldController.java         # auto world creation/reset
│   └── mixin/
│       └── GameRendererMixin.java   # end-of-frame hook
├── src/main/resources/
│   ├── fabric.mod.json              # mod metadata
│   └── netherite.mixins.json        # mixin config
└── env/                             # Python RL environment
    ├── netherite_env.py             # gym.Env
    ├── shmem_reader.py              # observation reader
    ├── shmem_writer.py              # action writer
    └── test_env.py                  # smoke test
```

## Phase Plan

### Phase 1: Mod works, pixels flow
- [ ] Gradle project builds on macOS
- [ ] MC launches, mod loads, auto-creates world
- [ ] FrameGrabber writes pixels to shmem
- [ ] Python script reads pixels, displays them (verify correctness)

### Phase 2: Full gym.Env
- [ ] ActionInjector reads actions, player moves
- [ ] StateExporter writes game state
- [ ] netherite_env.py step/reset works
- [ ] Benchmark: measure env/sec single instance

### Phase 3: Sodium + Lithium
- [ ] Add Sodium jar, verify it loads alongside mod
- [ ] Add Lithium jar
- [ ] Benchmark: measure FPS improvement

### Phase 4: Multi-instance + training
- [ ] Launch N instances on anvil
- [ ] Pipelined training loop
- [ ] Benchmark: env/sec at B=16
- [ ] Compare to MineRL baseline

## Machines

- **local/macbook**: MacBook Pro M4 Max, 36GB, macOS. Development + testing.
- **anvil**: Ryzen 9 9950X3D, 92GB DDR5, RTX 3090 24GB, Ubuntu 24.04. Training at scale.
  - Access: `ssh anvil` (Tailscale, VPN can be flaky)
  - Java 21 installed, CUDA 13.2 (for PyTorch only, not rendering)
  - Xvfb for headless GL
