# Design: mob AI in the blaze GPU path (CUDA first, Metal mirror)

Author: Fable (netherite session 2026-08-22). Reviewer: codex (13 findings,
folded into the v2 revision below). Status: agreed spec for the port-matrix
`mobs` row. `entity_spine` closed 2026-08-22; `projectiles` still blocks
`mobs` in port_matrix.

## Goal and non-goals

Goal: batched mob simulation across N envs (1024 typical) in blaze, bit-equal
to the verified magma CPU reference (`magma/game/mob_live.c`, detmob arc,
6/8 tapes bit-exact vs Java oracle). Same gate standard as every other port
row: magma-vs-blaze-CPU lockstep digest equality (M1), blaze-CPU-vs-CUDA
bitwise (M2), then Metal vs CPU on Mac. No tolerances, no fitted constants.

Non-goals: performance-first approximations; any AI behavior magma has not
already bit-verified; dragon-fight RL (out of scope per GATES).

## Why this is tractable

The whole mob tick is scalar integer + float arithmetic with two hard,
Java-imposed bounds that make GPU shapes static:

- `PathFinder.findPath` (oracle-src PathFinder.java:61-68): the A* loop breaks
  at `i >= 200` heap-pops. Successors per expansion come from a fixed
  `PathPoint[32] pathOptions` (PathFinder.java:17).
- Per-expansion cost/priority math is float `distanceManhattan` + `costMalus`
  (PathFinder.java:84-99) - a handful of float add/mul/compare, exactly
  reproducible with `__fadd_rn/__fmul_rn` (no FMA), the same discipline that
  already gives blaze CUDA bitwise parity on existing rows.

## Data layout (SoA, per env x mob slot)

Arrays (all SoA over env*mob):
- pose: x,y,z (double), yaw,pitch,yaw_body (float), vx,vy,vz (double), on_ground
- ent RNG: seed48 (u64) - java.util.Random LCG, integer-exact on GPU
- type, alive/persist flags, hp, hurt/death timers
- AI state: task bits, wander target, panic state, target idx, see/stime,
  attack timers, swell, anger - the det fields mob_live already carries
- path: `PathPoint`-equivalent SoA pool per mob, current index, total length
- navigate state: ticksAtLastPos clock, lastPosCheck, stuck check fields

World reads: mobs need block lookups (WalkNodeProcessor). Blaze already owns
the block store per env for existing rows; the node processor reads through
the same accessor the dig/movement code uses. Dependency: `world_dynamics`
row (already required by port_matrix for mobs).

## Execution shape: the batch unit is the ENV

- Magma completes each mob fully in slot order; collision couples pairs
  (mob_live.c:2458) and A* success feeds the same-tick caller branch
  (melee start mob_live.c:1493, repath-fail +15 attack delay :1571).
  A deferred/batched pathfind kernel cannot exist without a continuation
  machine; an async A* queue is therefore ruled out for bit-exactness.
- **One warp per env** (matching the existing blaze CUDA kernel shape,
  blaze_cuda.cu:474/497 repeat loop). Lane 0 executes the entire mob tick
  for its env sequentially - mobs in slot order, A* inline and synchronous.
  Lanes 1-31: idle in v1; later only pure block/AABB reads with commits in
  Java order. Parallelism = 1024 envs, which is the batch the trainer
  already feeds.
- No request queue, no host readback, no graph conditionals.
- Order inside a tick matches magma exactly: ai -> path -> move -> body, and
  mob index order within an env is the array order magma uses (which already
  matches Java's entity list order on the verified tapes).

## Bit-exactness rules (the actual answer to "which PTX")

- No hand-written PTX. CUDA C compiled `-fmad=false` for these translation
  units, with explicit rounding intrinsics at ported float sites. Integer ops
  (LCG, hashes, heap indices) are exact by construction. sm_120 on anvil;
  same flags as the rows that already pass bitwise CUDA-vs-CPU.
- Doubles for pose/motion exactly where Java uses double; floats where Java
  uses float; casts at the exact bytecode cast sites (the detmob method:
  javap on the deobf jar decides, never MCP source).
- Node identity: reproduce Java's IntHashMap *aliasing* - key is makeHash
  alone (IntHashMap.java:37, NodeProcessor.java:44); colliding (x,y,z)
  triples alias to one PathPoint. Do NOT resolve collisions. closedSet is
  cleared but never read (PathFinder.java:55) - omit it.
- WalkNodeProcessor.getStart iterates a HashSet<BlockPos> with observable
  order (WalkNodeProcessor.java:78); the existing shared port admits the gap
  (blaze/core/path_node_processor.h:578). v1: fail loudly if that branch is
  hit until a Java-backed fixture defines the order.
- M1 is blaze-vs-MAGMA: port magma's actual semantics, not Java's - the
  32x24x32 block window + reduced classifier (mob_live.c:664/726), the
  out-of-window neighbor rejection (:1019), the 48-point stored-path cap
  (:1038, mob_live.h:89). Factor into shared MC_HD code so magma and blaze
  compile one source. All EIGHT detmob tapes must be blaze-exact (the two
  Java-divergent tapes are still magma-deterministic).
- FP census before code: distanceTo uses float sqrt (PathPoint.java:67),
  navigation uses double sqrt/div (PathNavigateGround.java:176), AI uses
  log/sqrt/sin/cos for Gaussian+idle (mob_live.c:517/1197); mc_atan2 is
  host-only today (blaze/core/mc_math.h:63). Every primitive needs a
  device implementation plus a CPU/CUDA bit-pattern test. `-fmad=false` is
  necessary, not sufficient.
- RNG state = seed48 + Gaussian cache flag + cached value (mob_live.h:129).
  Consumption order within a mob tick is the sequential program order of
  that mob's own code - no cross-mob RNG interleaving exists in the det path
  (proven by the erng cursor equality on 8 tapes).
- Warp divergence is a performance concern only - bitwise equality does not
  care which lanes stall. No behavioral masking tricks.

## Capacities (derived, keep separate counters)

- Heap insertions <= 1 + 199*8 = 1593 (199 dequeues, 8 options ground);
  live heap <= 1394; path chain <= 199; point-map openPoint calls <= 3186
  at stepHeight 0.6. Overflow counters + fatal asserts before shrinking the
  existing 16384 pool (path_node_processor.h:236). Stored path stays 48
  (magma cap).
- Mob capacity: shared 96-slot entity store (ew_entity_store.h:21), not 16.
  A smaller training profile is a separate capability that rejects
  oversized fixtures; capacity is a compile-time constant, never a
  runtime gameplay knob.

## Prerequisite: M1 transport

Landed: snapshot v3 + `blaze_snap_mobs_digest` in `blaze/env/blaze_snapshot.h`
(magma writer `rl_mode.c` / `gm_mobs_export_snap`, blaze-CPU hashes the
static loaded store and does not step). v2 files load as zero mobs. Field
list and remaining entity-arc blockers: `blaze/OPEN_DIVERGENCES.md`
prerequisites.

## Metal mirror

Same kernel decomposition in MSL, `-ffast-math` off, precise float policy;
sim can also stay on the fixed CPU driver on Mac (as RL does today) until
the CUDA row passes M2 - Metal port is sequenced last, same as other rows.

## Evidence plan (gates before any merge)

1. Port detmob tape scenes into blaze snapshot fixtures (all 8 tapes).
2. M1: magma-CPU vs blaze-CPU lockstep digest per tick over those scenes
   (pose + RNG cursor + path outputs in the digest).
3. M2: blaze-CPU vs blaze-CUDA bitwise on the same scenes + randomized
   worldgen seeds, 1024 envs, multi-thousand ticks.
4. Perf smoke: 1024-env step time delta with mobs on vs off (budget: < 10%
   regression on the chain trainer's env step; measure, don't guess).
5. Never: tolerance relaxation, `--update` blessing, skipping the 200-cap or
   32-option semantics for speed.

## Sequencing

port_matrix `mobs` depends on `world_dynamics`, `entity_spine`,
`projectiles`. `entity_spine` M1+M2 VERIFIED 2026-08-22 (zero-intent
Entity.move / land travel). Remaining order: (1) projectiles, (2) this
design. This doc exists so the mobs row starts with an agreed shape, not to
jump the queue.
