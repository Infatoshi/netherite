# Netherite v2

MC 1.20.1 + Sodium + Fabric RL environment. 10x MineRL throughput target.
Full architecture in SPEC.md. Read it first.

## Build & Run
```bash
JAVA_HOME=/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home ./gradlew build   # build mod jar
JAVA_HOME=/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home ./gradlew runClient  # launch MC with mod
```

### Pixelated Agent View (160x90 stretched to window)
```bash
JAVA_HOME=/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home ./gradlew runClient \
  -Dnetherite.instance_id=0 -Dnetherite.seed=12345 -Dnetherite.rl=false \
  -Dnetherite.width=160 -Dnetherite.height=90 \
  -Dnetherite.uncapped=true -Dnetherite.max_fps=9999 \
  -Dnetherite.render_distance=4 -Dnetherite.graphics=fast \
  -Dnetherite.particles=minimal -Dnetherite.clouds=off \
  -Dnetherite.smooth_lighting=false -Dnetherite.entity_shadows=false \
  -Dnetherite.biome_blend=0 -Dnetherite.vsync=false \
  --args="--width 854 --height 480 --username player"
```
MC renders at 160x90 internally (full FOV), FramebufferMixin GL-blits it stretched to 854x480 window with GL_NEAREST (blocky pixels). FrameGrabber reads 160x90 from the FBO for shmem. Window shows what the agent sees.

### Run Tests
```bash
JAVA_HOME=/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home ./gradlew build -x test  # skip tests for speed
uv run pytest tests/  # Python unit tests
```

## Key Constraints
- Java 21 required (build uses `--enable-preview` for FFM/Panama semaphores)
- `JAVA_HOME` must point to OpenJDK 21 (e.g. `/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home` on macOS)
- Fabric Loom 1.9-SNAPSHOT (needs Gradle 8.12)
- Shmem paths: `/tmp/netherite_*` on macOS, `/dev/shm/netherite_*` on Linux
- UV for all Python. No bare python/pip.
- Sodium/Lithium jars go in `run/mods/` (modCompileOnly in build.gradle, runtime-loaded)
- Sodium LWJGL version check bypassed in build.gradle (`-Dsodium.checks.issue2561=false`)
- No emojis. No em dashes.

## Shmem Protocol
- Obs: `netherite_obs_{id}_{A,B}` -- 8MB double-buffered, 16B header + RGBA pixels
- State: `netherite_state_{id}` -- 64KB, player pos/health/inventory/entities
- Action: `netherite_action_{id}` -- 4KB, movement + camera delta
- All little-endian. Ready flag at offset 12 written LAST.

## Architecture
- GameRendererMixin hooks end-of-frame for PBO readback (render thread)
- ActionInjector + StateExporter + WorldController run on ClientTickEvents.END_CLIENT_TICK
- FrameGrabber uses PBO double-buffer: async glReadPixels into PBO[N%2], map PBO[(N+1)%2]
- One frame of latency on pixels (fine for RL)

## Mixin Summary

| Mixin | Target | Purpose |
|---|---|---|
| GameRendererMixin | GameRenderer | HEAD: skip-render mode. TAIL: trigger FrameGrabber PBO readback |
| FramebufferMixin | MinecraftClient | @Redirect `framebuffer.draw()` in `render()` -- GL blits FBO to screen stretched (160x90 -> 854x480) with GL_NEAREST |
| WindowMixin | Window | Disable Retina scaling, hide window in headless mode, disable VSync when uncapped, override getFramebufferWidth/Height to render resolution |
| ClientFocusMixin | MinecraftClient | Keep game running when window loses focus |
| ServerTickMixin | MinecraftServer | Uncapped TPS (removes 50ms sleep) |
| RenderTickCounterMixin | RenderTickCounter | Uncapped FPS |
| ClientTickProfilerMixin | MinecraftClient | Per-tick timing profiler |

### Pixelated View: How It Works
1. WindowMixin overrides `getFramebufferWidth()/Height()` to return 160x90 when `netherite.width/height` are set
2. MC creates its internal Framebuffer (FBO) at 160x90 and renders the full scene into it
3. FrameGrabber reads 160x90 pixels from the FBO via PBO for shmem (correct for RL)
4. MC calls `framebuffer.draw(160, 90)` to blit FBO to screen -- FramebufferMixin intercepts this
5. Instead of MC's deferred shader-based blit, FramebufferMixin does a direct `glBlitFramebuffer` from FBO 1 to FBO 0 (screen) at the actual GLFW window size (854x480) with GL_NEAREST filtering
6. Result: full-FOV pixelated Minecraft filling the window

Key insight: MC's own `Framebuffer.draw()` goes through `RenderSystem.recordRenderCall()` (deferred), but direct `glBlitFramebuffer` executes immediately and is visible after `glfwSwapBuffers`.

## What NOT to do
- Don't write any custom rendering code. Sodium handles all rendering.
- Don't use the CUDA rasterizer. It's archived in ~/1.8.9/cuda-rasterizer/.
- Don't use Forge. This is Fabric only.
- Don't use MC 1.8.9 or 1.7.10. This is 1.20.1.
- Don't use /dev/shm paths on macOS (doesn't exist). Use /tmp.
- Don't use `@ModifyVariable` on `Framebuffer.draw()` -- it targets a deferred render call lambda and has no visible effect. Use `@Redirect` on the call site in MinecraftClient instead.

## Machines
- local/macbook: MacBook Pro M4 Max, 36GB, macOS 26.3. Development + testing.
- anvil: Ryzen 9 9950X3D, 92GB DDR5, RTX 3090, Ubuntu 24.04. Training at scale.
  - SSH: `ssh anvil` (Tailscale, VPN can be flaky from Shenzhen)

## Performance Tuning

Measured throughput (M4 Max, 160x90, Sodium+Lithium, uncapped):
- `step`: ~400 SPS, `step_sync`: ~213 SPS, `tick_only`: ~484 SPS
- Reference: Prism Launcher + Sodium on 1.21.11 = ~1000 FPS (our mod overhead accounts for the gap)

### Observation Modes
- `obs_mode="both"` -- pixels + voxels (default)
- `obs_mode="voxels"` -- skip frame capture (~10% faster)
- `obs_mode="pixels"` -- skip voxel sampling

### Key Parameters
- `step_ticks` -- game ticks per Python step (1=every tick, 4=every 4th tick). Higher = more throughput, less granular control.
- `skip_render` -- skip OpenGL rendering entirely (testing only, ~2x Java TPS but no pixels)
- `uncapped` -- remove 20 TPS server limit (required for high throughput)
- `use_semaphore` -- use POSIX semaphores for IPC signaling instead of polling (~7% faster)

### step_ticks Benchmark (single instance, obs_mode=both)
| step_ticks | Python SPS | Game TPS | Use Case |
|------------|------------|----------|----------|
| 1 | 290 | 290 | Fine-grained control |
| 2 | 170 | 340 | Balanced |
| 4 | 110 | 440 | High throughput |
| 8 | 60 | 500 | Max throughput, coarse control |

### Performance Bottlenecks (in order of impact)
1. **Python-Java IPC** (~1.5ms/step polling, ~1.4ms with semaphores) -- mmap sync overhead
2. **MC Client Tick** (~700us) -- input processing, network, player updates  
3. **MC Server Tick** (~400us) -- game simulation, entities, blocks
4. **MC Render** (~420us) -- OpenGL draw calls + PBO readback

### Improvement Roadmap
- [x] Uncapped server TPS
- [x] Configurable observation modes
- [x] Skip-render mode for testing
- [x] step_ticks parameter for frame skipping
- [x] POSIX semaphores (replace polling with kernel signaling) -- achieved +7% (394->422 SPS)
- [x] Pixelated agent view (160x90 rendered, stretched to window via GL blit)
- [ ] Batched trajectories (amortize sync across N steps) -- expected N-fold
- [ ] Multi-instance vectorization -- expected linear scaling
- [ ] VulkanMod integration (requires MC 1.20.4+, replaces Sodium, Vulkan via MoltenVK on macOS)
- [ ] CUDA-GL interop on anvil (zero-copy GPU frame readback, ~0.7ms/step savings, see SPEC.md)

## Prior Art (archived, don't use)
- ~/1.8.9/cuda-rasterizer/ -- custom CUDA software rasterizer, 93% pixel match but missing mobs/entities/particles
- ~/1.8.9/forge-workspace/ -- MC 1.8.9 Forge mod with SceneCapture, HeadlessMeshGenerator
- ~/netherite-v2/ on anvil -- partial Fabric 1.20.1 setup (mod builds, world creation incomplete)

## Current Session State (February 23, 2026)

### What Works
- Full build pipeline: `./gradlew build` succeeds (Java 21, Fabric Loom 1.9)
- Sodium 0.5.13 + Lithium 0.11.2 load at runtime (LWJGL check bypassed)
- Shmem IPC: pixels, state, and actions flow between Java and Python
- `step`, `step_sync`, `tick_only` all functional
- Pixelated agent view: 160x90 rendered, GL-blit stretched to 854x480 window
- Python unit tests pass (`uv run pytest tests/`)
- FPS benchmarks: Vanilla vs Sodium+Lithium measured and documented

### What's Next (Phase 4)
1. Multi-instance launch on anvil (Linux headless via Xvfb)
2. Pipelined training loop (Python gym env driving multiple MC instances)
3. Benchmark throughput at B=16 instances
4. Compare to MineRL baseline
