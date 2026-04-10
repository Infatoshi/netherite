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
| Sodium | mc1.20.1-0.5.13 | Modrinth maven (CaffeineMC, LGPL-3.0) |
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

### GameRendererMixin.java (~30 lines)
- @Mixin(GameRenderer.class)
- @Inject HEAD: skip-render mode (cancels render when `skipRender` is enabled)
- @Inject TAIL: triggers FrameGrabber.INSTANCE.onFrameReady() + render profiling
- This runs AFTER the full frame is rendered (world + entities + HUD + chat)

### FramebufferMixin.java (~40 lines)
- @Mixin(MinecraftClient.class) -- targets the call site, NOT the Framebuffer class
- @Redirect on `framebuffer.draw(II)V` inside `MinecraftClient.render(Z)V`
- When netherite.width/height are set, replaces MC's deferred shader blit with a direct `glBlitFramebuffer` from the FBO to the default framebuffer (screen)
- Stretches the small render (e.g. 160x90) to fill the actual GLFW window (e.g. 854x480) with GL_NEAREST filtering (pixelated)
- Key insight: MC's `Framebuffer.draw()` uses `RenderSystem.recordRenderCall()` (deferred lambda) which makes `@ModifyVariable` on the Framebuffer class ineffective. Direct GL blit bypasses this.

### WindowMixin.java (~60 lines)
- @Mixin(Window.class)
- Disables Retina framebuffer scaling on macOS (`GLFW_COCOA_RETINA_FRAMEBUFFER = FALSE`)
- Hides window in headless mode
- Disables VSync when uncapped
- Overrides `getFramebufferWidth()/getFramebufferHeight()` to return netherite.width/height (forces MC to render at low resolution)

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

## Version Choice

MC 1.20.1 was chosen for Sodium/Lithium mod ecosystem maturity and stable Fabric API/Yarn mappings. There is no hard technical reason against newer versions. Considerations for upgrading:

- **1.20.4+**: Minimum for VulkanMod (Vulkan renderer via MoltenVK on macOS, replaces Sodium's GL pipeline). No 1.20.1 build exists.
- **1.21.x**: Latest VulkanMod builds (0.6.1 for 1.21.11). Benchmarked at ~1000 FPS on M4 Max with Sodium in Prism Launcher.
- **Migration cost**: New yarn mappings, Fabric API version bump, mixin signature changes, FrameGrabber GL calls may need updating if VulkanMod replaces GL context.
- **VulkanMod vs Sodium**: Mutually exclusive. Both replace the renderer. VulkanMod uses Vulkan (lower dispatch overhead on macOS via MoltenVK), Sodium uses optimized GL (batched indirect draws). Cannot coexist.

## Performance Targets

| Metric | MineRL | Netherite v2 Target | Measured (M4 Max) |
|---|---|---|---|
| Frame readback | ~5ms (sync glReadPixels) | ~0ms (PBO async) | ~0.4ms (PBO) |
| Draw calls/frame | ~3000 (vanilla) | ~1-5 (Sodium indirect) | ~1-5 (Sodium) |
| Server TPS | 20 | 20+ (Lithium) | ~480 (uncapped) |
| Env step latency | ~50ms | ~16ms (60fps) | ~2.5ms (step), ~4.7ms (step_sync) |
| Throughput (B=1) | ~20 env/sec | ~60 env/sec | ~400 step, ~213 step_sync |
| Throughput (B=16) | ~20 env/sec | ~200+ env/sec | Not yet measured |
| Rendering correctness | 100% | 100% (same GL renderer) | 100% |

### Benchmark: Vanilla vs Sodium+Lithium (M4 Max, 160x90, headless, uncapped)

| Config | Variant | Vanilla | Sodium+Lithium | Delta |
|---|---|---|---|---|
| fps=260, RD=6 | step_sync | 143 | 213 | +49% |
| fps=260, RD=6 | step | 313 | 402 | +28% |
| fps=260, RD=6 | tick_only | 417 | 484 | +16% |
| fps=500, RD=6 | step_sync | 148 | 190 | +28% |
| fps=500, RD=2 | step_sync | 180 | 214 | +19% |

Reference: Prism Launcher + Sodium on MC 1.21.11 (same M4 Max): ~1000 FPS average. Gap vs our ~480 tick_only is due to per-tick mod overhead (StateExporter, ActionInjector, FrameGrabber, shmem writes).

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
│       ├── GameRendererMixin.java   # end-of-frame hook, skip-render mode
│       ├── FramebufferMixin.java    # GL blit stretch (160x90 -> window)
│       ├── WindowMixin.java         # retina disable, framebuffer size override
│       ├── ClientFocusMixin.java    # keep running when unfocused
│       ├── ServerTickMixin.java     # uncapped TPS
│       ├── RenderTickCounterMixin.java  # uncapped FPS
│       └── ClientTickProfilerMixin.java # per-tick profiling
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
- [x] Gradle project builds on macOS (Java 21 required, `JAVA_HOME` must point to OpenJDK 21)
- [x] MC launches, mod loads, auto-creates world
- [x] FrameGrabber writes pixels to shmem
- [x] Python script reads pixels, displays them (verify correctness)

### Phase 2: Full gym.Env
- [x] ActionInjector reads actions, player moves
- [x] StateExporter writes game state
- [x] netherite_env.py step/reset works
- [x] Benchmark: measure env/sec single instance

### Phase 3: Sodium + Lithium + Visual
- [x] Add Sodium jar, verify it loads alongside mod (0.5.13, LWJGL check bypassed in build.gradle)
- [x] Add Lithium jar (0.11.2)
- [x] Benchmark: Sodium+Lithium gives +28-49% step_sync, +28% step throughput vs vanilla
- [x] Pixelated agent view: 160x90 rendered at full FOV, GL-blit stretched to 854x480 window with GL_NEAREST

### Phase 4: Multi-instance + training
- [ ] Launch N instances on anvil
- [ ] Pipelined training loop
- [ ] Benchmark: env/sec at B=16
- [ ] Compare to MineRL baseline

## Future: CUDA-GL Interop (Anvil Only)

On anvil (RTX 3090), the current frame path has unnecessary CPU round-trips:

```
GPU renders frame → PBO readback (GPU→CPU) → shmem copy (CPU) → Python mmap (CPU) → numpy → GPU (training)
```

CUDA-GL interop eliminates the CPU entirely for observations:

```
GPU renders frame → cudaGraphicsGLRegisterImage → CUDA tensor (stays on GPU) → PyTorch policy
```

### Implementation Scope (~200 lines C + JNI bridge)

1. **C/CUDA library** (`libnetherite_interop.so`):
   - `cudaGraphicsGLRegisterBuffer()` to register MC's PBO as a CUDA resource
   - `cudaGraphicsMapResources()` + `cudaGraphicsResourceGetMappedPointer()` per frame
   - Exposes the mapped GPU pointer to Python via ctypes or a small pybind11 wrapper
   - `torch.as_tensor()` wraps the CUDA pointer as a PyTorch tensor (zero-copy)

2. **Java side changes**:
   - FrameGrabber exposes the PBO GL handle (already has it: `pbos[mapPbo]`)
   - JNI call or shmem-published PBO ID so the C library can register it
   - Alternatively: use a shared GL-CUDA texture instead of PBO

3. **Python side changes**:
   - Replace shmem frame reader with `interop.get_frame_tensor()` returning a CUDA tensor
   - Training loop consumes tensors directly, no numpy decode

### Expected Gains
- Eliminates ~0.4ms FrameGrabber shmem copy + ~0.3ms Python decode = ~0.7ms/step
- At current ~2.5ms/step, this is a ~28% reduction
- More importantly: frames never leave GPU VRAM, enabling higher resolutions without CPU bandwidth bottleneck

### Prerequisites
- Linux only (CUDA not available on macOS)
- MC must run with a real GL context (Xvfb on anvil)
- CUDA and GL contexts must share the same GPU
- Not compatible with VulkanMod (would need CUDA-Vulkan interop instead, similar API but `cudaImportExternalMemory`)

### Not Planned (for reference)
- CUDA-Vulkan interop: same concept but with `VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT` + `cudaImportExternalMemory`. Only needed if VulkanMod replaces Sodium on anvil.
- Metal compute on macOS: MPS (Metal Performance Shaders) for training. PyTorch MPS backend exists but is less mature than CUDA. Would need Metal-GL interop for frame capture which Apple doesn't support well.

## Machines

- **local/macbook**: MacBook Pro M4 Max, 36GB, macOS. Development + testing.
- **anvil**: Ryzen 9 9950X3D, 92GB DDR5, RTX 3090 24GB, Ubuntu 24.04. Training at scale.
  - Access: `ssh anvil` (Tailscale, VPN can be flaky)
  - Java 21 installed, CUDA 13.2 (for PyTorch only, not rendering)
  - Xvfb for headless GL
