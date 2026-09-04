# netherite

Netherite is a native Minecraft 1.11.2 decompile, replay, and training stack.
The Java game is the oracle. Magma is the playable game. Blaze is the batched
simulation and policy trainer. Verify compares native results with the oracle.

This file defines the target product. Old code does not extend this contract.

## Product layout

| Path | Owner |
|------|-------|
| `java/` | Java oracle, mod, source bootstrap, and game capture |
| `magma/` | Playable game and CPU, CUDA, and Metal raster backends |
| `blaze/` | Batched simulation, policy, and CPU, CUDA, and Metal compute backends |
| `verify/` | Tape format, capture control, replay, comparison, and gates |
| `netherite/` | Shared config and binary format contracts only |
| `out/` | Generated output. This tree is ignored and disposable |

Do not add a generic `scripts`, `tools`, `utils`, `helpers`, `common`, `misc`,
`data`, or `assets` directory. A file stays with the system that owns it.
`assets` means game input only.

## Language contract

| Work | Language |
|------|----------|
| Oracle and mod | Java |
| Simulation, replay, capture control, comparison, tests | C |
| NVIDIA compute | CUDA C with CUDA Runtime, cuDNN, and cuBLAS |
| Apple compute | C ABI, Objective-C host bridge, MPSGraph, and Metal shaders |
| Build | Make and Gradle |

The product has no Python runtime, Python build step, or LibTorch dependency.
The product has no project shell script except the Gradle wrapper (`gradlew`).
Make can execute system commands. Humans and agents invoke `./gradlew` for the
Java oracle.

C is not a place for unrelated tooling. Each executable and source file has one
owner and one permanent purpose.

## Commands

Java (oracle). Not C. Not CUDA. Gradle:

```text
cd java/Minecraft && ./gradlew -g run/gradle build
cd java/Minecraft && ./gradlew -g run/gradle runClient
```

`make -C java bootstrap-oracle` is first-clone only. It is a Java 8 program
that calls `org.gradle.wrapper.GradleWrapperMain`. It is not a play command.
There is no root `make oracle`.

Make (C / CUDA / Metal):

```text
make            host native backends (magma game; Metal on Darwin)
make assets     magma/assets/build.c
make test       short native units only (<180s)
make play       magma_game
make clean      owner clean targets
```

Do not add root `make verify` or `make train` until those C binaries exist and
are the only path.

Make targets select an operation. They do not select runtime behavior.

## Configuration

Every native program reads a flat `key = value` config. `--conf PATH` selects
a file. `--set key=value` provides a visible test override.

Magma, blaze, and the native trainer keep their own registries until one
native program reads a shared file. Runtime behavior never uses an environment
variable. A selected backend never falls back to another backend. An
unavailable backend fails with a clear error.

## Oracle

The unmodified Minecraft 1.11.2 behavior is the source of truth. The Java mod
records player input, world state, entities, packets, and frames. A C control
client starts capture and receives status through the QRL socket.

The oracle can use Forge, Malmo, and Gradle. Native game code does not link to
them.

## Tape and replay

Verify owns one versioned binary tape format. Java writes it. C reads it.

The format has a magic value, schema version, explicit byte order, field sizes,
record lengths, and content hashes. The reader checks every length before use.
The format does not write native C structs directly.

A tape contains the full run description, initial state, tick input, required
oracle state, entity state, and frame references. Committed tapes stay in
`verify/tapes/`. Scenario inputs stay in `verify/fixtures/`.

Replay tools run Magma or Blaze from the same config and action stream, report
state differences, and compare captured frames according to the gate used.

The current native replay command checks child exit status, complete row counts,
tick alignment, finite required values, and pose/vitals/dimension differences.
Any failure exits nonzero. State tick counts completed actions, so it is tape
action index plus one. Optional MCA hash diagnostics are not a pixel or full
world-state gate. Python trace tools still own full-frame comparison during
migration; `make -C verify test-replay-contract` tests the native CLI boundary.

Committed tapes are still JSONL. The C replay driver reads that legacy format
and emits magma's event script. The hot path must not stay on JSONL: a
versioned binary tape replaces it when Java writes that format.

PNG remains valid for committed image input and failed-frame output. Verify
uses one pinned native PNG implementation.

## Magma

Magma owns the playable tick, world, input, audio, UI, and raster path.

Every runtime owns its player controller as `GmRuntime.ctl`. Stateful controller
APIs receive that object explicitly. `PlayerControlState` in
`blaze/core/player_control_state.h` declares the simulation fields shared with
Blaze; Magma keeps rendering/audio transients in its enclosing `GmPlayerCtl`.
Snapshot readers and writers map fields explicitly without changing wire layout.

The CPU backend is the reference. The CUDA and Metal raster backends consume
the same scene and return the same framebuffer contract. All backends use the
same config and asset pack.

`magma/assets/build.c` reads the owned game inputs and writes the runtime asset
pack under `out/magma/`. Generated C headers are not the asset format.

## Blaze simulation

Blaze owns the batched world state, tick, observation, reward, policy, and
training loop.

The CPU tick is the reference. CUDA runs the batched tick. Apple GPUs do not
support the FP64 tick contract. The Metal backend therefore runs the exact CPU
tick and moves observation and policy work to Metal. This is one supported
backend. It is not a second simulation.

Blaze dimension travel uses private mutable regions seeded from immutable named
snapshot banks. Returning to a dimension preserves its edited cells, light and
biomes. Arrival search and placement use the same ring-order routine as Magma,
including coordinate scaling; snapshot player poses are not arrival answers.
Unavailable portal-search coverage and missing banks fail execution. The finite
snapshot region remains the supported world boundary. Banks in one vector must
have compatible dimensions and seeds; conflicting bank paths are rejected.

The current snapshot wire format represents one region. Capture and dump reject
environments that visited another dimension, or have a failed transfer, until a
multi-dimension format exists. Reset discards episode edits and restores original
banks. End travel and vanilla nearest-distance portal selection remain outside
this implemented transfer contract. `make -C verify test-dimensions` checks the
selected CPU or CUDA library through its public ABI.

The native trainer's `world_size` setting selects the finite horizontal side
length in blocks. The recipe default is 64; height is inherited from the input
snapshot (128 in the shipped training fixtures). Zero preserves the original
extent. Positive sizes must be multiples of 16 in 32..256 and cannot expand
beyond available source data. Startup writes cropped copies under
`out/blaze/rl/worlds/`, including named dimension banks, source/prepared
checksums, measured bounds/resource counts, and the effective recipe. Source
fixtures are unchanged. Environment loading and reward-target extraction use
the same prepared inputs. Camera rays outside the finite region see its current
empty boundary; this mode does not claim full-world observation equivalence.

Leaving the horizontal region is an episode truncation, not player death. It
has no death penalty and uses the final observation's value before resetting.
Inventory and crafting milestones remain the curriculum's progress signal;
distance shaping can only describe targets known inside the stored region.
An absent target is unknown beyond that boundary, not evidence that the goal
does not exist elsewhere in the seed's world.

The intended larger-world design separates the active block cache from the
world's extent. Generate/load deterministic chunks as the player moves, retain
edits by dimension and chunk when evicting them, and preserve progress and goal
metadata independently of resident blocks. That moving cache is not implemented
by the current `world_size` setting. Increasing a fixed cube is not its substitute.

### Training recipes

`blaze/rl/ppo.conf` is the editable native training recipe. Compiled defaults,
then `--conf FILE`, then repeatable `--set key=value` determine its values.
Unknown keys, malformed values, invalid cross-field constraints and unsupported
policy modes fail. `--dump-config` prints all effective values, including reward
coefficients, curriculum weights and observation/action settings.

`phase_files` is an ordered comma-separated list of overlay configs. Every
phase inherits the global recipe independently; omitted settings do not leak
from a previous phase. Each phase has its own `max_chunks`, `max_ticks` and
`max_wall` limits and learning-rate schedule. Limits are checked at completed
update boundaries. Worlds, starting snapshots, reward assistance, episode/action
duration and curriculum controls may change by phase. Policy and Adam optimizer
objects stay alive; the optimizer update counter must advance through every
phase. Episode/reward/curriculum state and captured starts are rebuilt for the
new phase. The existing bootstrap at the end of a rollout remains valid before
the phase reset. This is a scheduled phase transition, not chunk streaming.
On Linux, environment drivers and their runtimes remain mapped until process
exit because OpenMP workers can outlive an environment. Destroying an environment
still frees its state; phase teardown must not unload code under those workers.

Backend/device, environment count, rollout length, minibatch layout, precision,
random seed, observation/action contract, warm-start input, checkpoint path and
evaluation contract must agree across phases. Those changes require a separate
run. `init_from` loads weights only and resets Adam; it is not an exact training
resume. Primary and derived best checkpoint outputs cannot alias that input.

All `reward.*` coefficients and `reward.shaping_scale` are configurable; the
scale affects dense assistance, leaving milestone rewards and penalties intact.
`curriculum.*` controls mastery threshold, history window, minimum episodes,
stage weights and seed weights. Seed weights index `train_seeds` order, and
zero excludes a seed from sampling. Stage zero retains positive weight as the
available fallback. Starting inventory, terrain and hazards come from the
selected fixture or stage snapshots, preserving their actual game state.
`world_min_logs` and `world_min_coal` reject unsuitable prepared starts instead
of silently removing them from the recipe. `spawn_yaw_jitter` and
`spawn_pitch_jitter` deterministically vary prepared starting angles by source
content and seed; they do not resample on every reset or modify dimension banks.

The policy tensor stays 18x36x64 plus 27 scalars. Configurable inputs include
one/two-frame history, seven semantic channels, depth, edges and scalar groups.
The `obs_base_scalars` group contains the existing coal direction/distance hint
and camera pitch; disabling it removes all six values, including information
about coal outside the visible image. Inventory, height and episode clock have
separate switches. These are explicit training aids, not ordinary RGB pixels.
`obs_pixel_stride` coarsens samples then expands back to the same tensor, so it
does not claim reduced model resolution or compute. Camera angle increments and
the nine action-head enable bits are configurable. RGB and recurrent memory
require model changes and are not supported settings. Checkpoint
`.policy.conf` sidecars identify this exact contract; mismatches fail before
weight loading. Legacy weights without metadata are accepted only with the
exact default observation/action contract and an explicit warning.

`blaze/rl/eval.conf` independently fixes seeds, starting stage, world size,
episode budget, repeat count and deterministic/sampled actions. It can run as
`out/blaze/rl/eval --conf FILE --set checkpoint=PATH`. Evaluation emits per-episode
JSON and the effective recipe; a `.score` file exists only for complete, finite
Blaze evaluations. Missing requested inputs fail by default, and explicit partial
diagnostic evaluation cannot select a checkpoint. Magma evaluation reports no
return because its interface has no reward output, and it does not emit a score.
Training enables periodic evaluation with `eval_conf` and `eval_every_chunks`;
`checkpoint_metric=eval_success` selects best weights using completed evaluation
episodes rather than training reward. The effective evaluation recipe is copied
once into the run directory. Users must choose evaluation seeds distinct from
training seeds when measuring transfer; the historical default seed list alone
does not guarantee that separation.

Every invocation creates a private directory beneath `run_dir` containing the
global recipe, effective phase recipes, fixed evaluation recipe when enabled,
transition/optimizer events and evaluation results. World manifests remain next
to prepared snapshots under `out/blaze/rl/worlds/`. `make -C blaze/rl test-recipe`
checks the parsers, reward/curriculum behavior, policy contracts, phase constraints
and world preparation. Executable CPU/Metal/CUDA runs are still required to
validate a changed trainer path; a parsed knob alone is not evidence it works.

Runtime random values use a counter-based or hash-based protocol. The result
does not depend on thread order or backend scheduling.

## Blaze policy

`blaze/nn/nn.h` defines one C ABI. The ABI covers create, forward, sample,
update, load, save, and destroy operations. Backend details do not cross it.

One model declaration owns tensor names, shapes, types, and weight order. CPU,
CUDA, and Metal use that declaration. The checkpoint format stores a magic
value, schema version, model hash, tensor table, byte order, type, shape, and
payload hash.

The CPU policy is the readable reference. It favors direct loops and clear
operator order.

The CUDA policy uses:

- cuDNN for convolution forward and backward operations.
- cuBLAS or cuBLASLt for linear forward and backward operations.
- Small CUDA kernels for conversion, activation, sampling, log probability,
  entropy, PPO loss, GAE, reductions, gradient clipping, and Adam.
- Fixed parameter, activation, gradient, and optimizer arenas.
- One CUDA stream contract and no allocation in a training step.

The CUDA backend does not link PyTorch, LibTorch, ATen, or a general tensor
framework.

The Metal policy uses a C ABI and a small Objective-C bridge. MPSGraph owns the
first correct training graph. The process compiles fixed shapes once and keeps
weights, activations, gradients, and optimizer state on the device. Raw Metal
kernels can replace measured hot operations. A performance guess is not enough
to add a second path.

FP32 is the correctness mode. A lower precision mode must pass the same fixed
fixtures before it can train or evaluate a policy.

## Verification

Native tests are C executables. Java tests stay in Java. Tests do not use a
second language as an oracle at run time.

Each policy backend must pass these fixed fixtures:

1. Every layer output.
2. Final logits and value.
3. Sampled actions with the shared random protocol.
4. One backward pass.
5. One clipped PPO update.
6. One Adam step.
7. Checkpoint save and load.

CPU and GPU floating-point results use declared error limits. Replay state uses
exact values where the game contract requires them. A test with no evidence is
an infrastructure failure. It is not a pass.

Performance reports include the host, device, driver, library versions, config,
fixture hash, warmup, sample count, and timing method. Do not report a speedup
without the baseline from the same harness.

## Host support

| Host | Required support |
|------|------------------|
| Linux x86_64 | Java oracle, CPU, CUDA, play, train, and full verify |
| macOS on Apple silicon | CPU, Metal raster, Metal observation, Metal policy, and native verify |
| Windows | Not supported |

Remote host access does not change the product contract. Missing host checks
stay pending until that host is available.

## Change rule

Do not translate an old tool because it exists. First decide if a permanent
gate or product command needs its behavior. Delete it if no owner needs it.

For required behavior, add the native replacement, compare it with the current
output, move its callers, and delete the old implementation in the same
milestone. Do not keep two command paths.
