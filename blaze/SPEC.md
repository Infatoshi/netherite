# blaze: Minecraft 1.11.2 simulation -> C/CUDA for batched RL

Goal: port as much of MC 1.11.2's *simulation* (world, blocks, fluids, light, entities, mobs,
crafting, win condition) into data-oriented C that compiles BOTH ways - single-instance CPU
(the reference oracle) and a batched CUDA megakernel (N parallel envs) - from one
`__host__ __device__` source. Rendering is NOT re-implemented here: render-opt
(`ref/render-opt`, 40 kernels bit-verified vs real MC) + MC's GL own the visuals.

This is the sim layer. It is the bigger, more important target than rendering.

The authoritative shipped-simulator scope is `../magma/PRODUCT.md`: survival-only
default-world completion through the End exit portal, vanilla-default superflat as an
RL arena, optional villages/enchanting/brewing/weather bundles, and explicit cuts for
redstone, audio, saves, multiplayer, achievements, and side content. Isolated kernels
outside that contract do not imply product support.

## Fidelity contract (the decision that makes this tractable)

INTERNAL CONSISTENCY, not vanilla bit-exactness. The oracle is:

    CPU scalar path  ==  CUDA batch path   (bitwise, same source compiled twice)

We do NOT try to match vanilla MC tick-for-tick. That deletes the three things that killed the
prior real-MC rewrites (netherite v0/v1):
- no need to match java.util.Random LCG *call order*
- no need to match HashMap/HashSet iteration order
- no need to match strictfp JVM bit patterns

Real MC 1.11.2 (`ref/mc-src`, decompiled, read-only) is the BEHAVIORAL SPEC and visual sanity
reference, not a bitwise oracle. Where we DO want vanilla faithfulness (worldgen terrain for a
given seed; physics constants; rendering compute), we use render-opt-style captured goldens.

Precedent: craftax.c hit 47.8M SPS pure C (3.2x an RTX Pro 6000) and craftax.cu hit 7x JAX -
both from-scratch, internally-consistent, scoped. That is the model. The faithful real-MC
rewrites were abandoned twice. We are not repeating that.

## The 5 determinism rules (non-negotiable; make CPU==CUDA hold)

1. RUNTIME RNG IS HASH-BASED + STATELESS, keyed on (tick, x, y, z, purpose). Order-independent
   => thread-schedule-independent => CPU and CUDA agree by construction. See core/mc_rng.h
   (`mc_hash_rng`).
2. WORLDGEN RNG replicates Java's 48-bit LCG exactly (core/mc_rng.h `JavaRandom`) so a seed
   yields recognizable terrain. This is the ONE seed-faithful subsystem.
3. DOUBLE-BUFFER THE WORLD: read tick N, write tick N+1. No in-place mutation a neighbor can
   observe mid-tick. This is what makes parallel ticking deterministic.
4. FLOAT DISCIPLINE (proven in render-opt): build C with -ffp-contract=off, CUDA with
   --fmad=false; replicate operator order left-to-right; explicit (int) truncation + NaN clamp.
   Goal is CPU==CUDA agreement (NOT JVM agreement, which we do not need).
5. SEQUENTIAL SUBSYSTEMS (light BFS, fluid FSM) -> iterate-to-fixpoint cellular automata,
   double-buffered. Propagation *timing* will differ from vanilla; that is allowed and must be
   documented per subsystem.

## Language: data-oriented C core, thin C++/host shell

- Hot path: plain C, SoA, POD structs, ZERO virtual dispatch, compiled `__host__ __device__`.
  May live in .cu/.cpp files to use light host-side C++ niceties. No std::vector/map or
  inheritance on device.
- C++ only at the edges: host orchestration, pybind11, test harness.
- MC's OOP is FLATTENED, not ported:
  - Blocks -> data tables indexed by packed state u16 = (blockId<<4)|meta (hardness, light,
    opacity, collision AABB, flags). Only blocks with real tick logic get a switch branch.
  - Entities -> tagged SoA + switch(entity_type); AI = small explicit state machines.
  - IBlockState -> packed int + precomputed property tables.

## Scope manifest (KEEP / CUT). src/ stays read-only; CUT = "not ported".

Total MC 1.11.2 src ~338k LOC. client/ (90k) = rendering, owned by render-opt. Sim port ~248k,
heavily reducible.

### CUT - infrastructure (~45k LOC, ~zero sim coupling) [updated 2026-07-09]
- network/ (18k) - no client/server split, direct calls
- command/ (12k) - direct API instead of chat commands
- nbt/ (3k), world/storage disk save - in-memory worlds only; harness snapshots are not saves
- realms/ (2k), scoreboard/ (2k), stats/ (1k), achievements, crash/, profiler/, creativetab/
- server/ (most of 9k) - keep only the integrated tick driver concepts

### CUT - redstone + automation [decided 2026-06-29]
- redstone dust/torch/repeater/comparator, piston + sticky, hopper/dropper/dispenser, observer,
  rails + all minecarts, tripwire, pressure plates, daylight sensor, dispenser/ pkg

### CUT - decorative / cosmetic [decided 2026-06-29]
- stained glass + stained clay (16x each), carpets, banners, heads, paintings, item frames,
  jukebox/noteblock, beacon, flowers/double-plants, dyes, music discs, maps, fireworks

### KEEP - speedrun/RL product path (see `../magma/PRODUCT.md`)
Terrain/biomes/caves, Nether fortress, stronghold, central End, survival blocks + ores,
water/lava, light, route-relevant passive/hostile mobs, combat, crafting/smelting, portals,
bed explosions, dragon/death/exit portal, and a terminal `won` observation. Villages,
enchanting, brewing, and weather are coherent runtime bundles that default off.

## Batched-env state layout (decided 2026-07-13; measured, not guessed)

Sizing census (MAGMA_STATE_PROF probe, `magma/game/script.c`, seed 0):
- distinct packed states: max 27/chunk, max 31 per 3x3-chunk window (pristine overworld
  spawn; End terrain 6-7) -> a 256-entry palette has 8x headroom
- non-air 16^3 sections: mean 5.2, max 6 of 16 (overworld; budget 8/chunk for Nether's
  0-128 fill)
- world edits, real 20k-tick gameplay tape: 1974 total = 0.10/tick mean (1267 of those
  are the t0 snapshot bulk-load; 99.9% of ticks edit 0 cells, rest 1-8)

HOT (HBM-resident, per env): chunks are 16 section slots, null = all air. A section is
4096 u8 palette indices + 4096 u8 light; palette is a PER-CHUNK append-only u8 -> u16
(id<<4|meta) table, 256 entries (measured max 27 - per-chunk beats per-env: no append
contention across parallel cells), overflow drops the write and the volume-hash gate
fails the same tick (see core/pal_chunk.h + pal_fluid_parity, PASS 2026-07-13).
Sections come from an allocate-once pool sized envs x chunks x 8; the prototype inlines
the 8 slots to stay POD/pointer-free. A 3x3-chunk env window is ~440 KB
vs 1.73 MB dense u16 (4x); obs gathers halve their bytes. mc_get/mc_set signatures are
UNCHANGED - the layout swaps behind the accessors, every system re-verifies through its
existing CPU==CUDA gate.

COLD (checkpoint/reset/replication): an env is (seed, dirty-edit journal, entity+player
state). Worldgen is pure in the seed, so reset = regen (+ replay journal); a full-episode
checkpoint is KBs (measured ~2k edits/episode).

Determinism rule 3 stays but its implementation changes: the 'next' buffer becomes the
journal. A tick reads 'now' in place, appends writes to a per-tick edit list, and applies
it at the tick barrier in deterministic key order - the measured 0.1 edits/tick makes the
full-window now->next copy (1.75 MB/env-tick, an HBM-bandwidth ceiling of ~500K SPS by
itself) strictly wasteful. Worldgen still writes sections dense-direct, then palettizes.

## Status (2026-07)

Waves 0–14 unit oracles are in-tree and marked verified under `oracle/goldens/` +
`cpu/`/`cuda/` drivers. Product wiring and live-Java traces continue in **magma**
and qrl tapes; isolated kernel PASS does not imply product support (see PRODUCT.md).

Open notes (code owns detail):
- `populate` Golden.java may lag live world_diff (stale-skylight / mushrooms); prefer
  genprobe + `magma/trace/world_verify.py` for worldgen truth.
- GPU worldgen: K1 noise GO for many-env RL; stage-split K2–K6 not built. Most `.cu`
  drivers are one-thread-per-env parity gates, not throughput engines.
- Prefer extending live tick/entity traces over adding more synthetic wave-1 harnesses.

Porting gotchas:
- C does NOT sequence `+`/`<<` operands; Java is left-to-right. Multi-RNG expressions
  need ordered temporaries.
- `a[i] = i++` is UB in C; write loops plainly.
- Float: `-ffp-contract=off`, CUDA `--fmad=false`; CPU==CUDA is the runtime bar.

## Layout

```
blaze/
  SPEC.md            <- this file (scope + rules)
  core/              <- __host__ __device__ shared C
  cpu/               <- single-instance scalar drivers
  cuda/              <- device drivers (parity / batch scaffolds)
  oracle/            <- goldens + runner
  py/                <- pybind11 gym smoke
  ref/               <- mc-src, render-opt, netherite-csrc symlinks
```

## Build / verify

- CPU:  cc -O2 -ffp-contract=off ...
- CUDA: nvcc -arch=sm_120 -O3 --fmad=false ...   (anvil = RTX PRO 6000 Blackwell)
- Oracle: `uv run --no-project python oracle/runner.py <name>` -> builds both, diffs bitwise.
- Python: UV only.
- History: `../../docs/DEVLOG.md`.
