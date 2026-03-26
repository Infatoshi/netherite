# Netherite v2

MC 1.20.1 + Sodium + Fabric RL environment. 10x MineRL throughput target.
Full architecture in SPEC.md. Read it first.

## Build & Run
```bash
./gradlew build          # build mod jar
./gradlew runClient      # launch MC with mod (shows window on macOS)
```

## Key Constraints
- Java 17+ target (use whatever JDK is installed)
- Fabric Loom 1.9-SNAPSHOT (needs Gradle 8.12)
- Shmem paths: `/tmp/netherite_*` on macOS, `/dev/shm/netherite_*` on Linux
- UV for all Python. No bare python/pip.
- Sodium/Lithium are runtime-optional deps (modCompileOnly, user drops jars in mods/)
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

## What NOT to do
- Don't write any custom rendering code. Sodium handles all rendering.
- Don't use the CUDA rasterizer. It's archived in ~/1.8.9/cuda-rasterizer/.
- Don't use Forge. This is Fabric only.
- Don't use MC 1.8.9 or 1.7.10. This is 1.20.1.
- Don't use /dev/shm paths on macOS (doesn't exist). Use /tmp.

## Machines
- local/macbook: MacBook Pro M4 Max, 36GB, macOS 26.3. Development + testing.
- anvil: Ryzen 9 9950X3D, 92GB DDR5, RTX 3090, Ubuntu 24.04. Training at scale.
  - SSH: `ssh anvil` (Tailscale, VPN can be flaky from Shenzhen)

## Prior Art (archived, don't use)
- ~/1.8.9/cuda-rasterizer/ -- custom CUDA software rasterizer, 93% pixel match but missing mobs/entities/particles
- ~/1.8.9/forge-workspace/ -- MC 1.8.9 Forge mod with SceneCapture, HeadlessMeshGenerator
- ~/netherite-v2/ on anvil -- partial Fabric 1.20.1 setup (mod builds, world creation incomplete)
