# Native Apple Silicon and Metal

This is the platform contract for the native macOS port. Linux/CUDA remains
supported and keeps its existing targets, binaries, gates, and performance
pins. The Metal path requires a native arm64 process; Rosetta is unsupported.

## Supported boundary

| Surface | CPU | CUDA | Metal on macOS |
|---|---:|---:|---:|
| magma game and simulation | yes | existing Linux path | yes |
| magma triangle raster | reference | existing full raster | native Metal raster |
| magma Overworld sky | reference | existing GPU path | native Metal sky |
| mc-sim full simulation | reference | existing kernel set | native CPU fallback |
| mc-sim parity kernels | reference | existing kernel set | RNG + semantic camera |
| blaze tick/reset | exact double | existing GPU path | exact CPU over shared state |
| blaze semantic camera | yes | existing GPU path | persistent Metal path |
| trainer tensor device | CPU | CUDA | PyTorch MPS |

The distinction in this table is deliberate. Metal Shading Language has no
`double`. Fidelity-critical worldgen, physics, snapshot, and action state are
not downcast to float32 merely to run on a GPU. The macOS product is fully
native and needs no CUDA, NVIDIA library, Rosetta process, or remote host; the
blaze backend is hybrid where precision requires it.

## Prerequisites and one-command setup

- Apple Silicon Mac running a current macOS release
- Xcode or the Xcode Command Line Tools
- SDL2 (`brew install sdl2 pkg-config` when absent)
- `make`, `uv`, and Python 3
- legal Minecraft ownership; no Mojang content is in this repository

```bash
bash scripts/setup_macos.sh
bash scripts/setup_macos.sh --full
```

Setup compiles and executes a native arm64 Metal probe, fetches the official
1.11.2 client only when needed, verifies the SHA-1 and size published in
Mojang's version manifest, regenerates ignored texture headers, builds every
native target, bakes local tick-zero snapshots, and runs the macOS sweep.

Shaders are compiled at runtime with `newLibraryWithSource`. Xcode's optional
offline Metal Toolchain component is not required. Runtime compilation also
keeps deployment independent of a checked-in `.metallib`.

## Build and run

```bash
# Game and raster
make -C c/magma game                 # magma_game, CPU only
make -C c/magma game-metal           # magma_game_metal, CPU + Metal
make -C c/magma game-cuda            # unchanged Linux/CUDA target
c/magma/magma_game_metal --backend metal --world superflat \
  --view-distance 4 --width 1280 --height 720
c/magma/magma_game_metal --backend metal --world superflat \
  --view-distance 1 --headless --ticks 100 --script events.jsonl \
  --frames-out frames.npy --render off --pace unlimited

# mc-sim Metal parity executable
make -C c/mc-sim metal-all
make -C c/mc-sim verify-metal

# blaze dynamic library
make -C c/magma blaze_metal_dylib
```

Backend requests are explicit and capability checked. `--backend metal` and
`VecBlaze(..., backend="metal")` return actionable errors if unavailable;
neither silently falls back to CPU. Legacy `--cuda` and `so_path=` callers
remain supported.

## Metal implementation

The Objective-C++ files only own device discovery, pipeline creation, checked
buffer sizing, command encoding, synchronization, and error translation. Game
and simulation logic remains in the C core. MSL-facing descriptors use fixed
width integer fields and offsets rather than C pointers; hosts assert their
sizes and offsets.

The magma raster allocates shared color, depth, triangle, texture, mip, and
lightmap buffers once and grows them only at a checked capacity boundary. A
16x16 tile owns its pixels and walks triangles in submission order, preserving
the CPU depth and blend ordering. Empty work and dispatch tails are explicitly
bounds checked. CPU and accelerator backends share a checked 2^25-pixel
framebuffer cap (large enough for 8K UHD); allocation leaves an empty, safe
framebuffer on failure. The Metal persistent buffers plus bounded atlas cache
are also accumulated against 45 percent of the device's recommended working
set before allocation.

The same persistent backend owns fixed-layout sky descriptors and the sun and
moon texture buffers. `CR_BACKEND_CAP_SKY` dispatches the Overworld sky before
ordered world layers while preserving the existing CPU hand, overlay, HUD, and
presentation order. Script and RL capture use the same `GmFrameCapture` object
as CPU/CUDA; Metal reads its composed world framebuffer back before the CPU
passes and writes either sparse tick-numbered PPM files or direct NPY output.
An explicit Metal request never selects the CPU raster as a fallback.

The blaze Metal backend allocates its environment, camera, scratch, action,
and output pools during creation. Each step runs the unchanged double-precision
C tick/reset against shared state, dispatches one Metal thread per
semantic-camera ray, waits at the command-buffer boundary, then performs the
CPU finalization that depends on the camera result. Shared buffers avoid
per-step region copies. Masked resets, snapshot loading/capture, all 13 action
fields, reward, and done state keep the existing C ABI.

MSL is compiled with safe math. CPU uses `-ffp-contract=off`; CUDA continues to
use `--fmad=false`. Tests require bitwise equality where the operation surface
permits it and report the first lane/pixel/field mismatch otherwise. Metal sky
is exact for the gated clear-noon and underwater cases. On the M4, sunset
measured one changed pixel with a 1-LSB maximum, and midnight measured 6 of
66,563 pixels (0.0091 percent), 18 changed channels, and maximum difference
117 from device `sin` in the isolated star hash. The composed 160x90 night
capture measured 2 of 14,400 pixels and maximum difference 19. Those measured
budgets, exact repeats, and first-difference diagnostics are enforced.

## Python and MPS

```python
from rl.blaze.blaze import VecBlaze

env = VecBlaze(256, backend="metal", output_device="host")
# output_device="host": NumPy views over persistent shared Metal storage
# output_device="mps": persistent MPS tensors updated by an explicit copy
# synchronize_mps=False: queue copies until the policy action's host fence
```

PyTorch does not expose a stable public import of an arbitrary external
`MTLBuffer`. The MPS option therefore performs a deliberate copy from the
shared NumPy view into preallocated MPS tensors. The default synchronizes each
copy. A trainer can set `synchronize_mps=False`, use `env.host_outputs` for
host-side control bookkeeping, and let the required sampled-action transfer to
the host fence the queued observation copy and policy forward pass. It never
presents a private MPS pointer to the C ABI. `sync_outputs()` is available when
code needs a fence outside that cadence. Transfer time and sync count are
recorded so the cost is visible.

For a 24 GB machine, start full tick-zero batches at `N=256`; the one-update
training smoke uses `N=32`. Creation sums the persistent Metal shared-buffer
pools and refuses to exceed 45 percent of `recommendedMaxWorkingSetSize`, with
required bytes, budget, and a computed approximate maximum batch size.
Override only for measured experiments with `BLAZE_METAL_MEMORY_LIMIT_MB`.
That guard does not include host snapshot archives (capped at 128 snapshots) or
PyTorch-owned MPS tensors. `host_snapshot_bytes` reports the former, and the
benchmark's peak RSS exposes combined process pressure.

## Verification and measurements

```bash
bash netherite_macos_sweep.sh --quick
bash netherite_macos_sweep.sh --full

# Individual gates
make -C c/magma test-metal
make -C c/mc-sim verify-metal
cd c/magma
uv run --no-project --with numpy python rl/blaze/verify_metal.py
BLAZE_BACKEND=metal N_ENVS=32 T_CHUNK=2 MB=16 \
  uv run --no-project --with numpy,torch python rl/blaze/mps_smoke.py

# Non-gating performance measurements
make -C c/magma bench-macos-cpu METAL_BENCH_FRAMES=600 METAL_BENCH_WARMUP=120
make -C c/magma bench-metal METAL_BENCH_FRAMES=600 METAL_BENCH_WARMUP=120
cd c/magma && uv run --no-project --with numpy python \
  rl/blaze/verify_metal.py --snapshot rl/out/snaps/s10_t0.bsnp \
  --bench-only --n 256 --decisions 100
```

The full sweep records per-step logs under the printed temporary directory.
The paired magma benchmarks report frame mean/percentiles and stage times for
the same still-camera CPU and Metal workload. The Blaze host benchmark reports
env-ticks/s, semantic rays/s, allocation size, and peak resident memory; the
separate MPS smoke reports explicit action and observation-copy timing. These
macOS measurements are separate from the RTX performance pins in
`docs/GATES.md`; no cross-device parity is implied.

The 2026-07-30 clean `--full` run passed every available native build, CPU
reference, Metal parity, Blaze chain, MPS, game, structural, and benchmark
step. It explicitly skipped the live Java-golden comparisons because no
working JDK was installed, and the harsh real-Minecraft pixel comparison
because the external `mc_frame.png` golden was absent. Neither skip covers a
native implementation gap.

### Measured Apple M4 result (2026-07-30)

Machine: Apple M4, 24 GB unified memory, native arm64, macOS 27.0 beta. The
paired magma runs used the same superflat still camera, view distance 1,
1920x1080, 600 total frames, and a 120-frame warm-up (480 measured).

| magma metric | CPU raster | Metal raster |
|---|---:|---:|
| mean frame / FPS | 63.228 ms / 15.82 | 16.625 ms / 60.15 |
| p50 frame / FPS | 61.685 ms / 16.21 | 16.428 ms / 60.87 |
| p95 frame / FPS | 73.202 ms / 13.66 | 20.437 ms / 48.93 |
| p99 frame / FPS | 99.062 ms / 10.09 | 22.393 ms / 44.66 |
| raster stage mean | 19.737 ms | 7.656 ms |
| sky stage mean | 38.687 ms | 0.921 ms |

In the final full-sweep pair, Metal improved end-to-end mean FPS by 3.80x, the
raster stage by 2.58x, and the sky stage by 42.01x. It measured 60.15 mean FPS
and 60.87 p50. An immediately preceding identical run measured 18.068 ms /
55.35 FPS (0.928 ms sky, 8.880 ms raster), so the 60 FPS product pin is
demonstrated but not yet repeatably held. The final Metal run additionally
measured 2.041 ms shared-buffer input, 0.529 ms output, 3.788 ms HUD, and 1.468
ms presentation per frame. These numbers retain the measured tail and the
observed run-to-run variance rather than smoothing either away.

The real seed-2 T0 Blaze benchmark at `N=256`, repeat 4, and 100 decisions
measured 98,013.4 env-decisions/s, 392,053.5 env-ticks/s, and 225.8 million
semantic rays/s. It reported 2,495.2 MiB of Metal shared buffers, 4.3 MiB of
host snapshot storage, and 2,432.6 MiB peak RSS. The deferred-sync MPS smoke
(`N=256`, 64 chunks, minibatch 256) rolled out 15,989 env-decisions/s (63,956
env-ticks/s), completed the Adam update in 0.534 s, and passed checkpoint reload,
evaluation, and masked reset. Its cumulative observation enqueue time was
11.605 ms; the sampled-action host fences totaled 53.244 ms. These
view-distance-1 M4 figures are not comparable to
the view-distance-8 RTX figures in the Linux/CUDA gate.

## Known limits

- FP64-heavy mc-sim worldgen and physics are native CPU work on macOS. The
  current Metal mc-sim oracle surface is RNG and semantic camera.
- Blaze uses Metal for semantic-camera work, not its FP64 tick/reset. It is a
  correctness-first native backend, not a claim to match CUDA throughput.
- The Metal game accelerates ordered world-triangle rasterization and the
  Overworld sky. Mesh and transform work, End sky, hand, overlay, and HUD
  remain native CPU stages. Raster command buffers are synchronized per draw
  and frame-end copies shared color/depth back for CPU composition and SDL or
  headless output.
- `output_device="mps"` requires an explicit copy. Its default is synchronized;
  training can defer that fence to the action transfer as described above.
- `magma_game_metal` supports interactive SDL, dummy-video frame loops, and
  script/RL headless `--frames-out` capture in PPM-directory and direct-NPY
  modes, including sparse original-tick numbering.
- Live Java-oracle regeneration requires JDK 8 and locally generated licensed
  oracle inputs. Their absence is an external limitation, not a Metal skip.
