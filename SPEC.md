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

The replay program runs Magma or Blaze from the same config and action stream.
It reports the first state difference. It then compares captured frames.

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
