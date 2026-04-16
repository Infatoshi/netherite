# Netherite v2

Use this file as the Claude Code handoff. It is intentionally high signal and current. Read `SPEC.md` for the full design. Read `AGENTS.md` for the stricter workflow rules.

## Non-negotiables
- Use `uv` for all Python commands. No bare `python` or `pip`.
- Java 21 is required.
- On macOS, use `JAVA_HOME=/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home`.
- On anvil, use `JAVA_HOME=/usr/lib/jvm/java-21-openjdk-amd64`.
- Python verification standard in this repo is `uv run ruff check . --fix` and `uv run pytest tests/`.
- Fabric multi-instance launches must use isolated `--gameDir` roots under `run/instances/<id>`. Sharing a game dir corrupts Fabric's `.fabric/processedMods` cache.

## Machines
- `local/macbook`: MacBook Pro M4 Max, 36 GB, Metal, macOS 26.3.
- `anvil`: Ryzen 9 9950X3D, 92 GB DDR5, RTX 3090 24 GB, Ubuntu 24.04.
- `anvil` display for headless GL is `DISPLAY=:2`.
- SSH to anvil with `ssh anvil`.

## Build, Run, Test
```bash
JAVA_HOME=/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home ./gradlew build
JAVA_HOME=/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home ./gradlew runClient
uv run ruff check . --fix
uv run pytest tests/
```

## Current Truth
- MC version is `1.20.1`, Fabric only.
- Rendering is Minecraft + Sodium. Do not add custom renderers.
- Sodium and Lithium are runtime-loaded from `run/mods/`.
- Sodium's LWJGL version check is bypassed in `build.gradle` with `-Dsodium.checks.issue2561=false`.
- Shared memory paths are `/tmp/netherite_*` on macOS and `/dev/shm/netherite_*` on Linux.
- `bench_scaling.py` now supports `--width` and `--height`.
- `grid_demo.py` now defaults to `320x180` capture for human-facing demos.

## Render Resolution Diagnosis
- The project does natively render fewer pixels. This is not a Matplotlib crop.
- The current low-res path works by overriding `Window.getFramebufferWidth()` and `getFramebufferHeight()` in `src/main/java/com/netherite/mod/mixin/WindowMixin.java`.
- `FrameGrabber` then reads that full low-res framebuffer with `glReadPixels`.
- `FramebufferMixin` then blits that full low-res framebuffer to the real window with `glBlitFramebuffer`.
- That means `160x90` is a true native render size today.
- The problem is that the whole Minecraft client framebuffer is shrunk to `160x90`, not just the agent observation.
- Vanilla HUD elements do not fit cleanly into `160x90`. Hearts and hotbar clip. This is why the human-facing demo looked cropped.
- `320x180` is currently the minimum sane native resolution for human-facing display. The HUD fits and the full frame is preserved.
- Proper long-term fix: decouple human display resolution from agent capture resolution. Keep UI/display at `320x180` or higher, and make agent capture a separate lower-res path.

## Human Demo Artifacts
- Native `320x180` single tile: `recordings/320_180_native.png`
- Native `1920x1080` single tile: `recordings/1920_1080_native.png`
- `160x90` vs `320x180` HUD comparison: `recordings/160_vs_320_comparison.png`
- `1920x1080` vs `320x180` native comparison: `recordings/1080p_vs_180p_comparison.png`
- `B=8` grid demo at `320x180`: `recordings/anvil_b8_batched_demo_320x180.mp4`

## Recent Stability Fixes That Matter
- `PosixSemaphore.java` had a Linux `O_CREAT` issue. The Linux flag is now correct, and `sem_post` is guarded against invalid handles.
- `BakedModelManagerMixin.java` guards `BakedModelManager.shouldRerender` when `stateLookup == null`. This removed a startup crash path at scale.
- The Python start latch no longer keys on `frame_hash`. It now ignores pure frame jitter and latches on stable pose/chunk/seed state instead. This was necessary for multi-instance stability.
- Startup tracing was added in `env/startup_trace.py` and integrated into launcher/benchmark paths for bring-up debugging.

## Multi-Instance State
- `B=8` is now stable enough to benchmark on anvil.
- The main remaining architectural issue is not startup corruption. It is the display/capture coupling described above.

## Known Good Benchmark Settings
- Render distance `4`
- Simulation distance `5`
- `max_fps=9999`
- `uncapped=true`
- `use_semaphore=true`

## Latest Benchmarks

### Local MacBook, single instance, render enabled
Settings: headless, `RD=4`, `SD=5`, `max_fps=9999`, semaphore on, `300` measured steps after `75` warmup.

| Resolution | `step_sync` | `step` | `state_only` | `tick_only` |
|---|---:|---:|---:|---:|
| `160x90` | `217.4` | `666.4` | `741.2` | `730.7` |
| `320x180` | `192.7` | `592.6` | `753.8` | `617.6` |

Interpretation:
- `320x180` costs about `11%` on the render-coupled step paths on the MacBook.

### Anvil, apples-to-apples `1` and `8` env sweep, `160x90`
Command source is `env/bench_scaling.py` with `--envs 1,8 --strategies sync,batched,async --steps 100 --warmup 10 --width 160 --height 90 --render-distance 4 --simulation-distance 5 --max-fps 9999 --use-semaphore`.

| Envs | `sync` | `batched` | `async` |
|---|---:|---:|---:|
| `1` | `216.0` | `359.7` | `329.8` |
| `8` | `135.4` | `490.9` | `638.8` |

Raw log: `recordings/anvil_bench_160x90_apples.log`

### Anvil, apples-to-apples `1` and `8` env sweep, `320x180`
Command source is `env/bench_scaling.py` with `--envs 1,8 --strategies sync,batched,async --steps 100 --warmup 10 --width 320 --height 180 --render-distance 4 --simulation-distance 5 --max-fps 9999 --use-semaphore`.

| Envs | `sync` | `batched` | `async` |
|---|---:|---:|---:|
| `1` | `184.7` | `1020.5` | `312.7` |
| `8` | `122.9` | `439.3` | `567.6` |

Raw log: `recordings/anvil_bench_320x180.log`

### Anvil `320x180` vs `160x90` at `B=8`
- `sync`: `135.4 -> 122.9` which is `-9.2%`
- `batched`: `490.9 -> 439.3` which is `-10.5%`
- `async`: `638.8 -> 567.6` which is `-11.1%`

Interpretation:
- Treat the `B=8` penalty for `320x180` as about `10%`.
- The `1-env batched` result at `320x180` is clearly not stable enough to use as a comparison point. Do not build conclusions on that number.

## Current Best Human-Facing Resolution
- Use `320x180` for any human-facing native display or recording.
- Do not use `160x90` when you need a faithful full HUD view.

## Current Best Agent-Facing Assumption
- `160x90` is still fine for a pure agent observation if HUD fidelity is irrelevant.
- The codebase does not yet properly support `display=320x180` with `agent capture=160x90` as separate knobs. That is the next important rendering change.

## Commands That Matter

### Local human demo
```bash
JAVA_HOME=/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home ./gradlew runClient \
  -Dnetherite.instance_id=0 -Dnetherite.seed=12345 -Dnetherite.rl=false \
  -Dnetherite.width=320 -Dnetherite.height=180 \
  -Dnetherite.uncapped=true -Dnetherite.max_fps=9999 \
  -Dnetherite.render_distance=4 -Dnetherite.simulation_distance=5 \
  -Dnetherite.graphics=fast -Dnetherite.particles=minimal -Dnetherite.clouds=off \
  -Dnetherite.smooth_lighting=false -Dnetherite.entity_shadows=false \
  -Dnetherite.biome_blend=0 -Dnetherite.vsync=false \
  --args="--width 854 --height 480 --username player"
```

### Anvil benchmark at `320x180`
```bash
ssh anvil '
  cd ~/netherite-v2-bench &&
  DISPLAY=:2 uv run env/bench_scaling.py \
    --envs 1,8 \
    --strategies sync,batched,async \
    --steps 100 --warmup 10 \
    --width 320 --height 180 \
    --java-home /usr/lib/jvm/java-21-openjdk-amd64 \
    --render-distance 4 --simulation-distance 5 --max-fps 9999 \
    --use-semaphore --env-timeout 30.0
'
```

## Do Not Waste Time On
- Do not revive the old CUDA rasterizer or Forge code.
- Do not attempt `@ModifyVariable` on `Framebuffer.draw()`. It does not hit the visible path.
- Do not share a Fabric `gameDir` across instances.
- Do not treat the current `160x90` human display issue as a Matplotlib crop bug. It is a framebuffer architecture issue.

## Best Next Steps
- Implement proper separation between display resolution and agent capture resolution.
- Keep `320x180` as the human display baseline.
- Re-run `B=16` on anvil once the display/capture split is clean.
