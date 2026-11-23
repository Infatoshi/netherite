# netherite-v2 SPEC

Goal: a native (C, then GPU where measured to pay) reimplementation of
Minecraft 1.7.10 that is state-exact against the real Java game, fast enough
for batched RL, and playable. Same product as v1, rebuilt on a smaller game
with one tick implementation and gates that cannot pass while wrong.

## Decisions

2026-10-29 (Elliot): **no Python in the training loop.** "The point was never to use a Python trainer": everything at
minimal overhead, on the fastest kernels available, our own where none fit; any Python glue left needs a reason.
The trainer becomes native C and CUDA (item 6 of "The environment as configuration"); mcsr-speedrun's fastppo.py is
frozen as its reference, the way the Java client is the C engine's. The evaluation behind it is DEVLOG 2026-10-29
lane/native.

2026-10-27 (Elliot): **the environment is the product, configured like a PufferLib env, and brute force on one RTX
3090 is the bar** (GOAL.md). Seeds, actions (raw plus macro mods the policy chooses), the observation (with a Java mod
per version showing the same) and priors are configuration; every good run replays on the Java client. Measured the
same day (DEVLOG 2026-10-27 lane/intent): play that learns nothing reaches iron in 89 s of the CPU once it restarts
from what it already reached, where play from spawn saw no furnace in 6,528 episodes, so the recipe's first lever is
exploration (the start archive and its cells), ahead of the fidelity ladder below. The plan is "The environment as
configuration" at the end of this file.

2026-10-24 (Elliot): **the training recipe is a fidelity ladder.** A run starts on a low-fidelity sim that runs
fast to instill priors, then raises the fidelity in **intervals** so the model uses its priors and still hill climbs;
the batch may **fade**: some envs cross rung by rung, more of them over time. The ladder's rungs and their measured
costs are at the end of this file. Throughput on the sim is the main point: it is what makes the bottom rung's
brute-force priors affordable.

- **1.7.10 is the default version (Elliot, 2026-10-15).** 1.16.1 (its own branch) is the speedrunning version. In
  1.7.10 a run chooses by configuration the checkpoint it starts from, the sim's fidelity, and where each part runs;
  the choices, their measured costs and the command ledger are "The default version" below.
- **Version 1.7.10** (2026-09-22). Pre-1.9 combat, no 1.8 content, id+meta
  block storage, procedural block renderer, client chunk meshes built on the
  client thread (1.8.9 builds them on "Chunk Batcher" threads).
- **Java, then C, then the GPU (Elliot, 2026-09-25).** The product is the
  GPU implementation for batched RL. The C engine exists to be its oracle:
  C is anchored to Java every tick, and the GPU is checked against C,
  because a C replay is orders of magnitude faster than a Java one in the
  flywheel. So C is built around what the GPU will run, not around the human
  client: per-environment state in flat, fixed-capacity arrays (no pointer
  graphs or per-entity allocation the GPU cannot mirror), iteration orders
  carried as data rather than left to a container, bounded loops, the tick
  split into phases a kernel can be checked against one by one, frames drawn
  at partial tick 1.0 on request, no wall clock, the same float operation
  order (`-ffp-contract=off`). The human client (SDL input, interpolated
  frames, audio) sits outside the tick; it stays because played sessions
  find real divergences, but it gets no work the GPU does not need. v1's two
  ticks (magma, blaze) agreed on shared bugs because neither was anchored
  to Java; here each layer is checked against the one above it.
- **Oracle-anchored gates only.** Native state is compared against the Java
  oracle every tick with no oracle state injected during replay. No blessed
  residuals.
- **Unsupported features are world switches** (user, 2026-09-22). Tier 2
  and 3 stay in the Java source. `config.yaml` lists them; each is forced off
  with a "not supported yet, coming in a future release" notice, in the
  oracle as well, so both run the same game. Off means: horses, wolves and
  ocelots leave every spawn list; pumpkin golems and the wither are never
  built; mineshaft chest minecarts are built and filled (same draws) but never
  spawned, and minecarts and boats cannot be placed; no block ever receives
  redstone power and hoppers only store; enchanting table, the dev host, brewing
  stand and beacon are plain blocks (brewing stands never brew); empty maps,
  filled maps from loot, fishing rods, fireworks, book and quill do nothing;
  signs are placed blank; `/summon` refuses switched-off entities. A feature
  leaves the list when the native engine has it.
- **Villages are on** (user, 2026-09-22, reversing the first cut): village
  structures, villagers, trading and village iron golems are vanilla and in
  scope for the native port. All other worldgen is vanilla too: mineshafts,
  strongholds, temples and witch huts, dungeons, lakes, ores, trees,
  decoration, the Nether and the End. The only worldgen switches are the
  animal spawn lists and the mineshaft minecarts.
- **Engine: the GPU, checked against C** (see the decision above). Still
  on record from the first recommendation: build dirty tracking into block
  writes, and measure per-env tick cost per phase before writing a kernel.
- **Pixels: our own C renderer, matched against the oracle's software GL
  (decided 2026-09-24, Elliot).** The oracle's frames come from Mesa 26.0.3
  llvmpipe (software GL) under Xvfb on the GPU host - deterministic, open source, its
  rasterization math inspectable. We write the renderer from scratch in C: the
  fixed-function 1.7.10 pipeline (model-view-projection, triangle
  rasterization, GL_NEAREST texture sampling from the MC atlas, per-vertex
  colour and light, depth, blend, fog), matching llvmpipe's output pixel for
  pixel with the same assets. This is the point of the project, not a
  shortcut: the rendering math lives in C so the batched sim, the CUDA and
  Metal backends and RL share it and can point back to it. Reproducing GL's
  call stream would give none of that, so GL is the numerical ORACLE, not
  something we emit. The gate is pixels: pxdiff (tools/pxdiff.c) compares the native
  frame to the oracle golden frame and names the cause of any difference. v1's
  C rasterizer reached 0.96-1.66 mean error per channel against a *different*
  GL and never got exact; matching llvmpipe's known software math is the path
  to exact. apitrace is installed on the GPU host as a debugging aid (to read what GL
  emits), not as the gate.

## The oracle (built 2026-09-22)

Vanilla 1.7.10 client decompiled with MCP 9.08 from the user's own jar, a
plain source tree in `oracle/src` (one folder per package) that is generated and
never committed (2026-10-01): the pristine decompile, a scripted determinism
pass (`oracle/decomp/determinize.sh`, 107 files) and `oracle/decomp/oracle.patch`
(hand hooks in 14 vanilla files, the deleted screens), made by
`oracle/decomp/src.sh`; the harness they call is `oracle/harness/netherite/oracle`. Compiled with javac by
`oracle/Makefile`. The integrated server runs in the same JVM.

**Modes.** `--agent`: ticks advance only on command (JSONL script, tape
replay, or TCP). `--play`: a human plays at 20 Hz wall pace. Both modes run
the same lockstep, so a human tape replays exactly in agent mode.

**Lockstep.** Each client tick is paired with exactly one integrated-server
tick: client `runTick`, wait until every client packet is delivered (local
channel send/receive counters), permit one server tick, wait, wait until every
server packet is delivered. The two threads never run game code at once.

**Math.** Transcendentals use `StrictMath` (fdlibm) so results are identical on
x86-64 and arm64 JVMs and reproducible in C.

**Determinism.** Every unseeded RNG (`new Random()`, `Math.random()`, static
shared Randoms, `Entity` UUIDs) and the entity-ID counter are per thread role:
CLIENT (client thread in ticks), SERVER, OTHER (netty, IO), RENDER (client
thread while drawing). Static Randoms are seeded by name. Chunk IO is
synchronous. Agent-mode frames are a pure function of state: chunk renderers
are repositioned, queued, rebuilt, translucency-sorted and frustum-tested for
the current camera before drawing; entities draw from the first frame; the
vignette smoother advances per tick; `Minecraft.getSystemTime()` is tick time
(`tick * 50`). The join uses a non-pausing screen, so the server never
pause-saves.

**Pins.** The quiet-join pin (`Oracle.quietJoin()`,
oracle/harness/netherite/oracle/Oracle.java:35,63): while the client has not
joined, `World.updateEntities` (oracle/src/world/World.java:1896, 1954) skips
every non-player server entity and the weather effects. On by default for new
tapes (`quietJoinPin`), a replay takes it from the tape header's start block;
old tapes replay with it off. The native replay mirrors it as `quiet_join`
(csrc/engine/serverreplay.c). Vanilla behaviour without the pin is unchanged
once the client is joined.
The identity-hash pin (`-XX:hashCode=2` in `oracle/Makefile`) makes every
HotSpot object identity hash equal to one in fresh and pooled oracle JVMs.
In JDK 8, `HashMap` uses identity hashes to break equal-key-hash ties in a
tree bin. `World.activeChunkSet` can reach such a bin during the first
server tick: otherwise identical seed-5 starts visited four chunks in two
orders, then random-ticked different leaves. The C engine's
`jorder.c` already models the constant-one tie break. The pin changes no
Minecraft input or RNG seed, but it can change rows wherever Java iteration
previously depended on object identity. The harness ID changes with this
pin, separating the result cache and pooled JVMs from earlier recordings.
Existing recordings and snapshots remain historical references; the gates
and fresh replays check whether any of their rows move under this pin.
The calendar pin (`Oracle.calendar()`, `calendarMillis()`): while the oracle
runs, `World.theCalendar` starts at and `World.getCurrentDate` resets it to
2026-09-22 12:00 UTC, not the wall clock, so the date the bat spawn rule and
the Halloween pumpkins read (and the snapshot's `calMonth`, `calDay`) is the
same on every day a script is recorded. None of 1.7.10's dated rules fire on
it; the native replay reads the date from the snapshot manifest.
The save clock pin (`Oracle.saveClockMillis()`): the wall clock vanilla writes
into a save (level.dat's `LastPlayed`, so level.dat_old's; session.lock;
each region chunk's timestamp) is the same instant while the oracle runs,
and a chunk save queues a copy of its NBT (`ChunkIO.detach`: vanilla's shares
the live light and block arrays with the writer thread) that the oracle's
writer writes in save order, every save (a later save of a pending chunk
does not replace the earlier, which would leave timing-dependent stale
bytes in the region file's sectors), so two runs that
save one world (a checkpoint, SAVEEND) write the same bytes. None of it is
game state.
The tripwire pin (`Oracle.tripwireBounds()`, one hook line in
`BlockTripWire.func_150140_e`): the wire's entity test reads the block
singleton's bounds, which in vanilla are whatever the last
`setBlockBoundsBasedOnState` on any tripwire left (the client's pick ray, a
mob's sight ray or an arrow on the server thread, a chunk render), a race
between the two threads. While the oracle runs the wire first sets the bounds
of its own metadata, the box csrc/engine/serverreplay.c's sr_tripwire_check
uses (lane/playfuzz, recording pf-wirebounds-g16).
The anvil pin (`Oracle.anvilBounds()`, one hook line in
`Block.getCollisionBoundingBoxFromPool`): `BlockAnvil` keeps Block's
collision box, the singleton's bounds, which in vanilla are whatever the
last `setBlockBoundsBasedOnState` on any anvil left (the client's pick ray,
a chunk render, a ray on the server thread), so the server player's move
against an anvil reads bounds the client thread set: a race between the two
threads. While the oracle runs an anvil's box is its own metadata's (inset
1/8 across its facing), the box csrc/engine/collide.c uses (lane/playfuzz2,
recording pf-anvilbox-g2).
The client consumer pin (`Det.PIN_*`, no vanilla lines): vanilla's client
thread draws one Math.random stream and one seeder for everything in call
order, so the torch flicker, a spawned item's spin and bob phase
(`EntityItem.hoverStart`) and every particle's flight depended on every other
client draw of the session. While the oracle runs, `updateTorchFlicker`'s
eight draws, the `EntityItem` constructor under `handleSpawnObject`, the
particles' Math.random and own Randoms (not `EntityPickupFX`) and
`EffectRenderer.rand` take values from their own streams, each reseeded at
every client tick from `mix(mix(seed, 40 + k), t)`. The shared streams are
still drawn at every call a consumer makes, but a pinned value steers how
many calls some consumers make (an `EntityRainFX` lands, and draws in
`onUpdate`, at another tick; an `EntityCrit2FX` emitter's own Random decides
how many crits it builds), so d.cseed and d.cmath follow the pinned game,
not vanilla's; frames and the client fields change, no other row field does
(lane/clientgate re-recorded the six tapes made before it as `cg-NAME`).
The playable client mirrors it
(csrc/engine/det.h `det_pin`); every clientframes and rawjudge golden is
judged as made under it.
The spawner display pin (`Det.PIN_SPAWNER`, no vanilla lines; a tape's
`start.spawnerPin`, on for new tapes): the mob a
spawner shows (`MobSpawnerBaseLogic.func_98281_h`, made by the renderer on
the first frame that draws the spawner) draws its `EntityLivingBase`
constructor's three Math.random on the RENDER role's stream, whose state is
every earlier frame-time draw's; the third is the head's turn. While the
oracle runs those three come from one stream reseeded at each construction
with `pinSeed(seed, 5, 0)`, so every display mob of a world turns its head
the same way; the shared stream is still drawn (csrc/engine/det.h
`det_spawner_display_yaw`; play draws an older tape's head unturned,
lane/idxclient).

**Frame rate is not part of the game** (Elliot, 2026-09-25). Ticks run at 20
per second of game time; a frame is drawn after a tick, at partial tick 1.0,
only when one is asked for (`step {n, frame}`), so it is a pure function of
that tick's state and the pixel gates compare frames by tick, never by rate.
An RL env observes every n ticks: n = 4 to 20 is 5 to 1 frames per second,
and a batched GPU backend steps every env n ticks and renders once, since
one env's latency does not matter. The human client alone draws uncapped
frames between ticks with interpolation; those frames are cosmetic and not
gated (its state is).

**Input.** Recorded as effects, not devices. Per tick: `look` (yaw, pitch,
prevYaw, prevPitch at tick start); `gui` ops in order (`click` window slot
button mode, `close`, `respawn`); `in` snapshot after the vanilla mouse and
keyboard loops (key bindings held and pending presses, hotbar, focus,
leftClickCounter, ctrl); `opts` when a render option changed. Replay applies
the snapshot in place of the loops. Agents send `hold`/`press` short key names,
`look`/`dlook`, `hotbar`, `gui`; the oracle resolves them to the same form.

**Tape.** JSONL. Header: seed, `world` (the resolved config.yaml world:
seed, switches, the game rules that differ from vanilla's; the difficulty
and render distance are `options.difficulty` and `options.rd`; replay
takes the world from here), harness id (hash of patch + harness source),
pinned options. Row per tick: `t`, `act`, `cp` (client player), `sp` (server
player), `w` (world time, entity and block counts), `d` digests: `cp`/`sp`
(player NBT), `ents` (all server entities: id + NBT), `blk` (chained hash of
server block writes), `sw`/`sseed`/`smath`/`sstat` (server RNG streams),
`cw`/`cseed`/`cmath`/`cstat` (client tick RNG), `px` (frame RGB) when a frame
was captured.

**TCP control (Malmo replacement).** `127.0.0.1:PORT`, one JSON object per
line each way. `step {act, n, frame}` returns the row after n ticks (frame:
true or a path); `state`; `frame {path}`; `quit`. The first step waits for the
join.

## Gates (all green 2026-09-22, the GPU host)

| gate | command | result |
|---|---|---|
| concurrent identity | 3x `make script` smoke in parallel | 130 rows and 4 frames byte-identical |
| replay | `make replay` smoke | 130 ticks exact |
| frame schedule | `make replay EVERY=1` | 4 frames identical to sparse-render originals |
| negative | smoke with look +0.00001 deg at t=60 | DIVERGE t=60 d.cp, rc 3 |
| TCP | `tcp_smoke.sh` | step, state, frame, quit; re-render equals step frame |
| human replay | `play_synth.sh` seeds 3, 5, 11 then replay | 256, 257, 258 ticks exact |
| cross-platform | `make play` on the Mac (Apple Silicon JVM), replay on the GPU host x86 JVM | 393 ticks exact (idle session) |

## Native world core (planned 2026-09-22)

Population, block updates and every later tick write through one API, so it
is ported once, below everything else, as vanilla's `World.setBlock` does it:
`Chunk.func_150807_a` (sections created on demand, heightmap, column relight,
`propagateSkylightOcclusion`, block callbacks, tile entities), then
`World.func_147451_t` (the queued light engine, `updateLightByType`, which runs
only when every chunk within 17 blocks is loaded, so the loaded set is state),
then neighbor notification when flag 1 is set. Chunks come from the exact raw
pipeline; `Chunk.populateChunk`'s neighbor rule decides when population runs.

Each layer is checked by a probe: the oracle runs the operation on a region of
raw chunks far from spawn with population switched off, and writes the op list,
a per-op hash of the touched chunk, and the final state of every loaded chunk
(ids, metas, sky and block light, height maps, section mask, relight flags).
The native side replays the op list and must match every hash and the final
state; the first differing hash names the op. Order:

1. setBlock fuzz with plain blocks (no callbacks), flags 2: sections, height
   maps, sky light. Then with glowstone: block light.
2. Block callbacks the worldgen features trigger (liquids, falling blocks,
   leaves), and flag 1 notifications with scheduled ticks.
3. Population features one by one (ores, lakes, dungeons, trees, decorator,
   freeze), each probed on its own with a seeded Random, then `populate` whole.

## Native tick and tape replay (designed 2026-09-22)

The native engine is checked the way the oracle checks itself: replay a tape
and match it row by row.

- **Two sides, like the game.** In 1.7.10 the player moves on the client
  (input effects, EntityClientPlayerMP physics, then a C03 packet) and the
  server validates that move (NetHandlerPlayServer.processPlayer) and simulates
  everything else. The native tick keeps both halves in the oracle's lockstep
  order: client tick, deliver client messages, server tick, deliver server
  messages. Messages are structs, not bytes. The RL env may skip client-only
  work (render interpolation, particles) but never the client player's physics.
- **Start from snapshots, not worldgen.** The oracle writes a snapshot at a
  tape's start: every loaded server chunk (ids, metas, light, height maps,
  tile entities, pending block ticks), every entity's NBT, world info (time,
  weather, spawn), both players, and the Det stream states. The native harness
  loads it and replays from there, so physics, mobs, inventory and interaction
  are checked before population is exact. Once worldgen is complete a tape
  replays from its seed alone; that is the full gate.
- **Compare what the row carries, first difference wins.** The recorded values
  first (cp: position, motion, look, onGround, fall distance, health, food,
  hotbar; sp: position, health, food, saturation, XP, dimension; world time and
  entity count), then the digests as each system becomes exact: d.cp and d.sp
  (player NBT), d.ents (every entity's NBT), d.blk (the block-write chain), the
  RNG stream digests. The harness prints the tick, the field and both values;
  the tracepoint tool localizes inside the tick.
- **Det is state.** The oracle's per-role streams (seeder, Math.random, split
  Randoms, entity IDs and UUIDs) are ported bit for bit and carried in
  snapshots.
- **An unloaded chunk leaves memory, as vanilla saves it** (lane/envmem,
  2026-09-29). A chunk that leaves the view goes to its dimension's region
  store and, packed, to its environment's spill file (csrc/engine/regionspill.c,
  unlinked under ~/.cache/netherite-spill) with its tile entities, saved
  entities and saved ticks; its next load reads it back byte for byte. The
  other tables that grew with every chunk ever seen go too: an unloaded
  chunk's empty entity lists, the overworld spawner's records of mobs gone
  for good, saved-entity records nothing reads again, mineshaft pieces out of
  reach (to a second, append-only file). An exploring environment stays near
  its loaded area instead of growing about 37 KB an env-step.

Lanes, in order: (1) oracle snapshot command, native snapshot loader, tape
reader and comparison harness that matches row 0; (2) client player movement
(input effects, MovementInput, sprint, sneak and jump, moveEntityWithHeading)
and the server's validation, on top of moveEntity; (3) the server tick without
entities (time, weather, scheduled and random block ticks, chunk loading around
the player in PlayerManager order); (4) entities: the framework, item entities,
then mobs one by one; (5) interaction: digging, placing, containers, drops.
Each lane records its own synthetic tapes (walk, jump, sprint, swim, dig,
place, craft) with `make script`.

## Done, and what remains

Done means a human plays a fresh seed-1 world in `out/native/play` to the
Ender Dragon's death and the exit portal, and every session lines up with the
Java oracle row by row (`csrc/play/check.sh TAPE`: 0 field mismatches;
`csrc/play/session.sh` chains the sessions and checks each). The C engine
must be one-to-one with vanilla 1.7.10 before more GPU work (Elliot,
2026-09-27): no open in-scope record in `index/`, and the fuzzers and the
seed-1 chain clean. GPU work resumed on 2026-09-28 before the fuzzers are
clean (Elliot): `index/` is closed (875 of 875 in-scope files exact) and the
seed-1 golden chain passes, while the fuzzers still find causes at the seams
(death and dimension travel, the client's packets, save and rejoin, input)
that the GPU order ports last; lane/sweep keeps finding them beside the GPU
lanes, and a C fix to a phase already on the device is mirrored there.
Nothing counts on a delegate's word: the evidence is a commit, a recording,
a gate result, checked on master.

Proven (2026-09-27): the oracle harness and its gates; worldgen in all three
dimensions (35 overworld regions, the Nether and End regions, the spawn area
byte for byte on seven seeds); the golden playthrough on seed 42,
gold-g1-s42 to gold-g30-s42 from the seed to the dragon's death and the
return (271,218 rows, each segment native OK, Java replay OK, re-recorded
byte-identical); Elliot's session 1 (2,344 rows, 0 mismatches); the
human-play fuzzer's round of 244 sessions (164,700 rows, 0 mismatches);
the live client against 496 judged oracle frames (2.11 M px over 25); the
suite over 261 recordings.

Open:
- (`index/` closed 2026-09-28: every in-scope record exact. The coverage
  lanes of 2026-09-28 took the in-scope methods no recording ran, by area,
  in `make -C oracle coverage`'s unexecuted.tsv.)
- Coverage the fuzzer never reached: pearls and eyes, portal walking, boats,
  minecarts, riding, leads, fishing, TNT, potions, enchanting, anvils,
  brewing, trading; chained sessions through Save and Quit; the seed-1 route
  itself (stronghold, fortress, End).
- The live client's own randomness (torch flicker, particles, item phase).
- Elliot's sessions 2 onward.

## The GPU (design review 2026-09-28)

The review SPEC asked for is out/plan/gpu-design-2026-09-28.md
(lane/gpureview), measured on master on the dev host's RTX 3090 (sm_86, 24 GB,
PCIe Gen3 x8, 6.6 GB/s). What it found: the observation, not the tick, is
the cost (a steady C-renderer frame is 0.18 G instructions at 128x128 and
render distance 4, 2.38 G at 854x480 and distance 8, against 1.9 to 44 M
for a tick); the branchy tick compiled as one device thread per env (L17's
S6) runs about 40x slower per env than a host thread and fits 87 envs; the
generation kernels are 2x one host thread at batch 1,024 (their carvers
run a thread per chunk); the client world's own light is the new tail.
Plans before it: out/plan/gpu-audit-2026-09-25.md, gpu-audit-2026-09-26.md
(L11 to L17).

Decided (by Elliot earlier, or forced by the measurements):
- GPU against C, C against Java; kernels are gated by the phase digests
  (L6) through L13's harness, N recordings interleaved per launch, and the
  light, generation and renderer checks byte for byte.
- A C fix reaches the device through a rule the merge trial checks: a
  manifest of each device kernel's mirrored C functions and its quick
  gate; a merge whose changed lines touch a listed function runs that
  gate on the 3090 and is refused on a failure. (Without it L16 broke
  unseen on 2026-09-27: 4ab41c3b's client box rule, never mirrored.)
- The observation is rendered on the device at its own size, never large
  and downsampled; the device renderer is bit-exact against the C
  renderer at that size (no FMA contraction, correctly rounded division
  and square root, the same operation order, own sin and cos); the C
  renderer is matched to llvmpipe goldens made at that size (the oracle's
  --width and --height) within per-frame budgets, as at 854x480 today.
  Checked at 128x128: 0.25% of pixels over 25 per frame, 0.22% at
  854x480 (cov-zoo-s1).
- Frames at partial tick 1.0 after the step's last tick; steps of n ticks.

Decided by Elliot on 2026-09-28:
- D1, where the tick runs: hybrid. The C tick runs on the host as an
  asynchronous pool of worlds over its threads; the renderer (and the
  client world's light, D8) runs on the device; a tick phase moves to the
  device only when its kernel beats the host per env-step at its batch.
  His reasoning: whatever is intensive, parallel and not branchy goes to
  the GPU, everything else stays on the CPU, which we already have; this
  is not a pure-GPU system like vLLM or SGLang.
- Where it runs: the dev host only for now (its RTX 3090); later the GPU host, whose
  GPUs all sit on x16 links; never the Mac (the Mac plays csrc/play).
  Nothing goes to the GPU host until Elliot says so.
- The pipeline: the device keeps each world's render state resident and
  the host sends only what changed (the camera, entity poses, changed
  blocks and light: a few KB per world per step, a chunk-load burst about
  1.5 MB, projection); meshing happens on the device; observations stay
  on the device for the trainer (nothing large comes back). Worlds go in
  groups: the host steps one group while the device renders another, over
  several CUDA streams with double or deeper buffering, so small kernels
  still keep the device busy; a render batch is submitted when its queue
  fills. The group size, the number of streams, the buffer depth and the
  batch size are parameters, chosen per configuration (worlds, observation
  size, n) by a short sweep at startup: the cheap autotuner, a fuller one
  only if the best setting turns out to vary a lot.
- Layout by role (the v1 names magma and blaze are dropped: here one C
  engine is stepped and the device draws it): csrc/engine the C engine,
  csrc/cuda all device code, one folder per part with its own
  cuda/PART/PART.mk and gpu-PART targets (worldgen, lighting, meshing,
  render, tick; lane/cudanames), csrc/runtime the scheduler (the env pool,
  the pipeline, the trainer's API; runtime/pool.mk, runtime/pipeline.mk).
  The divergence ledger's two bridges are Java to C and C to CUDA.
- Taken as the review recommended, unless Elliot says otherwise: D6
  asynchronous stepping, D7 a device tensor the trainer reads in place,
  D8 the client world's light kept exact and deferred to the device, D9
  L17 kept as a correctness tool (cuda/tick).

Decided by Elliot on 2026-09-28, the observation (D2 to D5): 128x128,
square (vertical fov 70, so 70 degrees across), no HUD in the image (the
image is vanilla's with hideGUI, F1: no HUD, hand or block outline; a
screen still draws itself), and render distance 4. What the HUD shows goes
to the agent as numbers (health, food, armour, air, XP level and bar, the
selected slot, the nine hotbar stacks, the open screen and its slots),
along with the held item, which hideGUI also hides. Render distance 4 is
what 727 of the 784 snapshot recordings play at (the oracle's options, rd
4; 57 older ones play at 8); in 1.7.10 the integrated server's view
distance follows it, so it is game state as well as the observation's.
The observation's precision (Elliot, 2026-09-29, lane/renderprec) is part
of the spec too (pool_obs.prec, src/raster_prec.h): exact, the default and
the only mode judged against Java, or fast (reduced precision per stage:
float16 lighting, colour, fog and shading, float32 edges and reciprocal
weights), which the C renderer and the device draw identically, so every
gate stays byte equality within a mode; exact against fast differs in 3.7%
of the pixels, nearly all by one step (a few pixels at texel and triangle
edges by more, from the float camera offset and the reciprocals; DEVLOG
2026-09-29).
The observation's size is a parameter (Elliot, 2026-09-29, lane/obssize:
pool_obs w, h; pipe_gate and pipe_bench --size WxH, pipe-gate SIZE=,
pipe_play.sh -s), one size per pipeline; 128x128 stays the default.
64x64, 96x96, 192x192 and 256x256 are gated as 128x128 is: pipe-gate
default, MESH=2 DRAW=1 and MESH=1, every row and all 6,008 frames bit-exact
against the C renderer and play's own frames at that size, and the render
gate (1,694 frames, 744 variant frames, exact and fast) at each size.
Their cost at N 128 with device meshing on the GPU host (one RTX PRO 6000, 24
workers, 4 render streams; medians of three rounds, a round's spread about
10% from co-tenant load; out/perf/obssize.tsv), for 64 / 96 / 128 / 192 /
256: env-steps/s village 9,823 / 9,464 / 9,913 / 8,107 / 7,988, Nether
8,154 / 8,294 / 8,209 / 8,059 / 7,317, exploring 3,279 / 3,494 / 3,357 /
3,291 / 3,323; the renderer's device time alone 0.026 / 0.027 / 0.028 /
0.032 / 0.036 ms a frame (batch 128: geometry 0.019 at every size, bins
and raster grow with the pixels; the upload, the same bytes at every size,
0.02 to 0.04 ms more); device memory 13 bytes a pixel per env (two
observation buffers, the renderer's frame and depth: 0.05 to 0.85 MB) over
46 to 89 MB of meshes, atlases and arenas that do not depend on the size.
Every size is bound by the host's workers (the tick, 94 to 99% busy, the
same instructions at every size); in the village at 192 and 256 the four
render streams' ceiling comes within 5% of theirs. The device's kernels
are in flight 90 to 99% of the wall at every size, mostly the mesher's
(0.27 to 0.66 device-ms an env-step, independent of the size).
32x32 and 48x48 are gated the same way since 2026-10-15 (pipe-gate: 23,987 rows and 6,008 frames;
the render gate: 1,798 frames and 928 variants, each bit-exact). At configs/tests/gold-fast.yaml on the
dev host's 3090 (DEVLOG 2026-10-15, the observation's size) the device is the limit and the size
moves it little: N 128 26,023 env-steps/s at 128x128, 27,600 at 96, 29,184 at 64, 30,345 at 48
and 31,428 at 32, as most of a frame's device work is per section, quad and entity; at 32x32 raster's
few tiles (16 a frame) leave the device 74% busy. A pixel student distilled from the semantic teacher
kept 92.0% of its logs at 32x32 and 96.2% at 128x128 over the same 25 minutes.
The camera is a setting too (client.camera; never a row): pixels, the device renderer's bit-exact frame at
client.observation's size (the default); semantic, the semantic camera alone (csrc/runtime/semcam.h: block id,
metadata, distance and face a ray at client.semantic's size, no renderer); both; and raycast (lane/raycast,
csrc/runtime/raycast.h), the textured ray cast the CPU draws at the observation's size: RGB from the renderer's own
block table and terrain atlas, biome tints, the lightmap, water, entities as boxes, sky and fog, no renderer, gated
byte-equal against its reference (raycast-gate). At configs/tests/gold-fast.yaml, N 128, one tick a step (DEVLOG
2026-10-15, two lanes): the tick alone 121,000 to 125,000 env-steps/s; pixels 29,098 at 32x32, 27,953 at 64x64,
25,512 at 128x128; semantic 80,587 at 32x32, 38,168 at 64x64, 12,077 at 128x128; raycast 77,070 at 32x32 (0.168 ms of
a worker's thread a cast), 53,035 at 48x48, 37,033 at 64x64, 12,044 at 128x128 (2.29 ms). The CPU casts win up to
64x64; the device from between 64x64 and 96x96 (a projection from the cast's cost a ray). engine.raycast device (lane/rcdev, lane/rcsplit:
cuda/raycast) runs the rays on the GPU over a ring copy of each env's client world, every frame and seen record the
CPU's (gpu-raycast-check); engine.raycast_host_groups K keeps K of the vector env's groups on their workers' cast. In
the trainer on the 3090 (DEVLOG 2026-10-19, the w2 arm's flags, 64x64): the host cast 49,303 to 50,691 env-steps/s at
320 worlds in 4 groups; the device cast 85,151 to 85,732 at 320 in 2, 100,057 to 100,223 with mcsr fastppo --compile
max; the learner the limit there.

Order of work (D1 is taken): the mirror rule and L16's repair; the device
renderer; the client light on the device; generation served ahead from
host threads; tick kernels behind the win gate (S6's random ticks first,
S11, the client and the network last or never: they change daily).

## The default version: what a run chooses, and the command ledger (2026-10-15)

1.7.10 is netherite's default version (Elliot, 2026-10-15; 1.16.1, its own branch, is the speedrunning one). A run
chooses three things by configuration (config.yaml's grammar, csrc/engine/worldconf.h), each measured below on the
dev host (9950X3D, 16 cores; the RTX 3090) with pipe_bench's corpus mode (configs/gold.yaml, removed 2026-11-23: seed 42, normal, render distance 4, at spawn, 20 steps warm
and 200 measured, 128x128, 4 ticks a step) in a window with nothing else on the GPU (the mirror nightly held at a
stop for it). Keys marked native are the runtime's alone: config.yaml leaves them out, the oracle refuses a file
that sets one, and the gates refuse a fidelity that is not vanilla.

- **The checkpoint**: `pipeline.start` (or pipe_bench `--start NAME=`) is a directory or a name in
  configs/checkpoints.tsv: fresh (seed 1), and on the seed-42 golden chain spawn, stone, food, iron, diamonds,
  portal, nether, nether-return, armor, pearls, pearls-mid, eyes, stronghold-walk, stronghold, end. Every gold
  segment loads and steps 8 envs in 1.0 to 1.8 s.
- **The fidelity** (native: the `fidelity` section, csrc/engine/fidelity.h): spawning, spawn_types, mobs (all, none
  or EntityList names joined by +), random_ticks, weather; the daylight cycle is world.daylight_cycle, a vanilla rule.
  User instructions an env-step (tick alone N 16; the pipeline N 64): vanilla 6.70 and 8.35 M; spawning off 2.83 and
  4.06; mobs none 2.19 and 3.36; random ticks off 6.17 and 7.59; weather off 6.64 and 8.23; spawning, mobs, random
  ticks and weather off 1.58 and 2.45. configs/tests/gold-fast.yaml (spawning, random ticks and weather off) against
  gold.yaml, the default performance settings: 11,571 against 8,830 env-steps/s at N 16, 22,197 against 14,078 at
  N 64, 23,830 against 14,169 at N 128. After the kernel round of 2026-10-15 (lanes kcbin, kcgeom, kcmesh) gold-fast
  runs 23,967 at N 64 and 25,815 at N 128 against 22,247 and 23,996 before it in the same session (DEVLOG). The renderer's fast precision (pipe_bench --render-prec fast, frames no
  longer bit-exact) gained nothing measurable at 128x128 (-2 to +5% at N 64 and 128), so it is not a setting.
- **The placement**: engine.device_mesh (host 0, device 1; 2 meshes on both and compares, a check),
  engine.device_generation, engine.device_light, and (native) engine.render (device, none) and engine.tick (host;
  device is refused as not implemented). env-steps/s, gold.yaml:

  | placement | N 16 | N 64 | N 128 |
  |---|---|---|---|
  | device meshing (gold.yaml) | 8,830 | 14,078 | 14,169 |
  | host meshing (config.yaml's) * | 5,083 | 4,479 | 4,122 |
  | all on the device but the tick (+ device_light, device_generation) * | 2,114 | 2,067 | out of device memory |
  | render none (the tick alone) | 22,704 | 28,598 | 26,174 |

  (* measured before the performance defaults below changed: 3 streams and 10 workers at N 64, 4 and 8 at N 128.)

  The tick alone at the fastest fidelity (gold-fast.yaml with mobs=none, render none), the sim without pixels: 93,162
  to 103,189 env-steps/s at N 32 to 256 on 16 workers, 117,378 at N 128 on 32 (469,511 ticks/s).

  The device light engine is the slow part of the all-device combination (4 to 5x slower an env-step) and its
  per-env collect buffers do not fit 24 GB at N 128. The renderer on the host is not offered: a steady C-renderer
  frame is 0.18 G instructions at 128x128 and render distance 4 (the GPU review, 2026-09-28), about 300 env-steps/s
  on 16 cores, against the device's 10,000. The tick on the device: L17's S6 (the chunk loop compiled for the device,
  one thread an env) ran about 40x slower an env than a host thread and fit 87 envs (2026-09-28), the work of about
  two host threads on the whole card; it would pay only as a data-parallel rewrite of the tick (entities and chunks as
  parallel work, gated phase by phase by the phase digests), which no measurement yet asks for: with the fidelity
  off the tick is 1.58 M instructions an env-step and the renderer's device work is the limit.
- **Performance** (native: the `performance` section): workers, streams, group, batch, depth, layout, pin, place,
  tune. 0 and pipe are the pipeline's own rules (pipeline.c params_fix, default_threads): with device meshing 2
  render streams and one and a half workers a core (24 on the dev host); with render none a worker every CPU (32:
  gold N 128 21,792 to 26,515 env-steps/s, gold-fast 76,079 to 89,361, equal digests); measured against the rules before (3 or 4
  streams, a worker a core less two a stream: 10 at N 64, 8 at N 128), interleaved on equal trajectory digests: gold
  N 64 10,703 to 14,078 env-steps/s, N 128 7,761 to 14,169; gold-fast N 64 20,167 to 22,197, N 128 19,627 to 23,830;
  village N 64 7,948 to 10,639, N 128 6,047 to 10,465; speedrun.yaml N 64 10,022 to 11,750, N 128 7,821 to 11,941;
  the nether checkpoint N 64 6,979 to 10,201, N 128 5,016 to 8,115; the end N 64 7,684 to 10,162, N 128 5,381 to
  10,880 (the means of two rounds); N 16 unchanged. The tuner (performance.tune, pipe_bench --tune) sweeps streams 1 to 3 by those two worker counts
  in the steady state in 5.7 to 7.0 s and picks 2 streams; before, it reset every env for each window, took 19 to
  27 s and chose 1 stream at 39% under the default, and the swept pipe itself ran at a third of a fresh one's rate
  (8,700 against 24,500 env-steps/s), so pipe_bench now measures a pipe made fresh with the choice.

### Command ledger

Every command of the 1.7.10 loop, its wall time on the dev host (load 1.5 to 8 unless named), and its budget: the
inner loop about 1 s, a run, a profile or a gate about 10 s. Over budget is a task.

| command | what | wall | budget |
|---|---|---|---|
| `make -s -C csrc all` | the engine and its tools, nothing changed / one engine file edited | 0.06 s / 1.5 to 2.0 s | 1 s |
| `make -s -C csrc/runtime pipeline` | pipe_bench, pipe_gate, playview.so: nothing changed / after an engine edit / a runtime file edited / a fresh tree from ccache | 0.18 / 0.25 to 0.32 / 0.66 to 0.70 / 11.3 s | 1 s (a fresh tree once) |
| `make -s -C csrc/runtime rlbind` | libneth.so, the trainer's library (a PIC build of the engine): nothing changed / one engine file edited / a fresh tree from ccache / after the night's header changes | 0.19 / 0.47 / 9.8 / 19.5 s | 1 s (a fresh tree once) |
| `make -s lane NAME=x` | a lane's worktree | 2.7 to 4.1 s | 10 s |
| `make -s -C csrc worldconf-check` | the config reader against config.yaml and every configs/ file | 0.07 s | 1 s |
| `make -s -C csrc globals` | the tick's writable globals | 0.64 s | 1 s |
| `make -s -C csrc index-check` | the codebase index (exits 2: 64 records with errors, the same before and after tonight) | 0.15 s | 1 s |
| `make -s -C csrc stack` | frames under 4 KB, chains under 32 KB (incremental) | 0.5 to 0.9 s | 1 s |
| `make -s -C csrc smoke` | the refactor check: 235 jobs | 6.4 to 7.1 s | 10 s |
| `make -s -C csrc clangcheck` | the Mac's compiler on the tree | 7.7 to 8.0 s | 10 s |
| `test_snapshots out/java/snapshots/cov-villagerep-s42` | one recording replayed row for row | 0.48 s | 1 s |
| `make -C csrc phase-profile RECS=cov-villagerep-s42` | one recording's phases: instructions, time, allocations | 1.05 s | 10 s |
| `make -C csrc/runtime pipe-gate PRECS="cov-villagerep-s42 nether-trip-s1 end-dragon-s1"` | the quick pipe gate: 32,967 rows, 8,252 frames bit-exact | 4.4 s (memo kept) / 8.4 to 10.7 s (memo made again after a playview.so change) | 10 s |
| `pipe_bench --config configs/gold.yaml --ns N --warm 20 --steps 200` | the corpus at N 16, 64, 128 | 1.3, 2.3, 3.8 s | 10 s |
| the same with configs/tests/gold-fast.yaml | | 1.2, 1.9, 2.8 s | 10 s |
| the same with `--start x=end --kinds x --ns 8 --steps 20` | a checkpoint loaded and stepped | 1.0 s | 10 s |
| the same with `--tune` | the sweep, then a fresh make and the corpus | sweep 5.7 to 7.0 s, the run 9.5 to 11.3 s | 10 s |
| the same with `--device-light 1 --gen-device` at N 64 | the all-device placement | 8.7 to 9.9 s | 10 s |
| `nsys profile -t cuda,nvtx --sample=none --cpuctxsw=none ... pipe_bench --config configs/tests/gold-fast.yaml --ns 64 --warm 10 --steps 100` | the end-to-end timeline | 5.1 s, then `nsys stats --report cuda_gpu_kern_sum` 0.8 s | 10 s |
| `out/flywheel/bin/ncuwrap.sh --kernel-name regex:raster --launch-skip 100 --launch-count 1 --section SpeedOfLight --section Occupancy --section LaunchStats -- pipe_bench ... --ns 64 --warm 5 --steps 20` | one kernel's counters | 6.9 s at gold.yaml (it waited up to 300 s for an empty GPU before 2026-10-15); 3.6 to 3.8 s a kernel at gold-fast.yaml | 10 s |
| `out/flywheel/bin/kcab.sh [-r R] [-n "64 128"]` | a kernel lane's A/B: master's pipe_bench against the lane's, gold-fast, rounds interleaved in one bench window | 3.5 s a run; 3 rounds at N 64 and 128 about 35 s, 11 rounds at one size 70 s | 10 s a run |
| `out/native/cuda/rcbench DUMP` | the cast kernel alone on a dump (rcdev_dump, mcsr scripts/rcdump.py): frames and seen records against the dump's, ms a launch; with its unit's build, the four dumps' corpus | 0.6 s; 2.4 s | 1 s |
| `make -s -C csrc gpu-raycast-check` | the device cast against raycast_ref and raycast_seen: six recordings at three sizes, 47,490 casts | 10.5 to 16.9 s (over: the recordings' replays) | 10 s |
| `out/flywheel/bin/automerge.sh LANE snapshots MSG` | a landing: the trial merge, the selected suite, master | about 2 minutes | detached |

Found over budget or broken while measuring, and fixed: the tuner (19 to 27 s, a worse choice), ncu's queue wait
(up to 300 s), `make stack` (it read src/ since the 2026-10-01 rename and stopped on its first pointer call; two
frames then over 4 KB, phase.c k_client's 15.6 KB copy of the client's packets and grave.c scan_written's pagemap
buffer, moved off the frame), `make smoke` (19 render jobs named recordings make test skips as retired; it skips them
now too). The first builds in a fresh tree (the pipeline 11.3 s, the trainer's library 9.8 s) and a landing stay
over and run once. The trainer's library takes the config's sections too: neth_rl_make on gold-fast.yaml at 64 envs,
every env stepped and polled together, 11,266 to 11,519 env-steps/s against 4,541 to 7,425 on gold.yaml.

## The fidelity ladder (the training recipe, 2026-10-24; three rungs in one engine, and what each must match)

One run walks up these rungs; a batch may hold envs on different rungs while it fades. Every rung is the one
engine: one `struct session`, one pool, one device cast, one reward and one macro implementation, so one PPO batch
can hold groups on different rungs. The rung is a per-environment field (`struct run_config.rung`,
`csrc/engine/env.h:147`, set from `pool_config.rung` — `csrc/runtime/pool.h:97`, beside `tick_batched` — in
`reset_job`, `csrc/runtime/pool.c:697`), and it is read at the top of the tick (`session_tick`,
`csrc/engine/session.c:1234`). **An env changes rung only at a reset**, never mid-episode: `neth_rl_state`'s chest
scan and window, and the trainer's reward baselines, describe the old rung's world (`rlbind.c:310-316`,
`vecenv.c:2483-2521`), and a server player left at spawn draws an S08 correction on the first exact tick
(`player.c:2213-2231`).

- **Rung 0, micro (`fidelity.rung: micro`):** the client half of the row, and nothing that needs a ticking server.
  `session_row_micro` (`session.c`, beside `session_row` at `session.c:1081`) runs the engine's own client tick
  (`client_player_tick`, `player.c:1672`), its pick (`player.c:1556`), its dig and place machines
  (`harvest.h:56,69`, `survival.h:980-981`), writes its blocks through `world_set_block` (so `wseq` and the light
  nibbles move and the cast's mirror reads them, `rcmirror.c:63-92`) and keeps the **server world loaded and
  frozen** for chunk delivery (`world_chunk_request`, `world.h:424`) and the chest scan (`rlbind.c:310`). One rule
  is modelled: a broken block's drop credited to `sp.sv.inv` and `STAT_MINE_BLOCK + id`; container and place
  ticks delegate to `session_player` (`session.c:1068`). Its gap: no mobs, no damage, food or death, no item
  entities, no random ticks, no day clock. Cost: **projected** T' 0.4 to 0.5 us a world tick (2.0 to 2.2x rung 2
  at k=20), from the k=20 attribution below: the client tick's 45% falls to a few percent, the server's chunk work
  and the entities (20%) to about 2%, and the waits, the staging and `session_tick` stay. It must match rung 2's
  client rows — position, look, `on_ground`, dig stage, mouseover, the client world's block writes and the cast's
  frame digest — **up to the first server-only event its tape's manifest names**, and no earlier.
- **Rung 1, stripped (`fidelity.rung: stripped`):** the exact engine with `fidelity.spawning: off`,
  `fidelity.mobs: none`, `fidelity.random_ticks: off` and `fidelity.weather: off` (`worldconf.c:66-70`) — the same
  game minus work, all of it already implemented. Cost: **projected** at most 1.25x rung 2 at k=20, because the
  server-side share of T is about 20% (15% chunk work and 5% entities). It must match rung 2's rows and frames on
  a tape where nothing the switches govern happens, and its first divergence elsewhere must be the manifest's
  first mob, weather or random-tick row.
- **Rung 2, exact (`fidelity.rung: exact`, the default, and the eval's rung):** today's engine, every row equal to
  the Java oracle. Measured: **120,446 and 120,525 env-steps/s** at 32x32 RGB, k=1, 30 workers named.

**Three axes, not rungs.** `engine.tick_scope: full|fast` skips five draw-side calls (`player.c:1707-1786`).
Measured (pipe_bench, 16x16 RGB, 64 envs, 30 workers, full fast fast full interleaved, equal digests,
lane/vecticks): at k=2 over 1,000 decisions **193.8k against 162.2k env-steps/s (+19.5%)**; at k=20 +47% over
decisions 10 to 40, +32% over 100 to 200, +25% over 400 to 600 and **+20%** over 800 to 1,000. The +49% first
quoted was the first of those windows (k=20, 30 decisions, `--counters`, one pair), labelled k=2. Before
lane/vecticks fast skipped the particle list's tick but not its spawns, so the list filled to its cap, `fx_add`
scanned and shifted all of it on every spawn and fast fell to 1% and then -13% against full late in an episode;
fast now empties the list each tick (only play draws it). It
was inert from `e776fc72` to `111cbfe3` (the pipeline's config copy and three call sites that build a
`pipe_config` field by field lost the flag), so every earlier "fast" number, the k sweep included, measured the
full client tick; the trainer's library on the GPU host still lacks the pipeline's copy. Its divergence is exactly three recorded fields, all client-side stream state the skipped work
used to advance: `d.cseed` (the client Random), `d.cw` (the client world, whose light-check cell `cp_world_tick`
relit) and `d.cmath` (the client's math Random, which the End's rows reach). Every other row field is equal: the
gate passes with `MASK="d.cseed,d.cw,d.cmath"` at 164,912 rows and 41,264 frames across `headline-s1`,
`nether-trip-s1` and `end-dragon-s1`, but those frames are the device's against the C renderer of the same fast
env, so they say nothing of fast against full. **What the observation sees differs** (lane/fastcast): without the
light check the client world keeps light full's would have rechecked, and the ray cast reads it. Held row by row
against full's cast (`make -C csrc/runtime raycast-fast-gate`, 32x32), fast's differs on **2,556 of 497,217 rows
(0.51%)** of both golden chains and the underground recordings, in 22 of 60, in runs up to **309 rows** of whole
frames darker where a torch's light was never rechecked (`gold-g29-s42` from row 1963). Keeping the check and
skipping only the particle loop does not fix it (2,511 rows, 25 recordings, runs to 376): full's check cell is
drawn from the client world's Random after that loop's draws, so only the loop's draws and block reads, most of
fast's saving, give full's cells. So fast is a training choice with a measured observation cost, and the eval stays
`full`. The observation's size and channels are a hard stage boundary, not a fade: `fc.0`'s input width is
`c2*ceil(h/8)*ceil(w/8)` (`fastppo.py:94`), so a 16x16-to-32x32 step is a new run from the last `model.pt` with
its Adam state (`--init`, `fastppo.py:726`), which is why no `grow`/`grow_opt` sits on the ladder's path. Action
repeat k is the vec env's: a group step runs the config's `client.ticks_per_step` times `client.action_repeat`
world ticks (`queue_group`, `vecenv.c`; the act hook holds the decision and drives a macro over the later ticks),
and the trainer's discount a decision is `0.998^k` (`fastppo.py`, `ticks_a_step`). Until lane/vecticks the vec env
stepped one tick a decision whatever the config said, so no training run before it had k above 1.

**The cost model** (measured in one chain, 2026-10-24, pipe_bench, 256 envs, 2 groups, 30 workers named, D and T
from five k values with the baseline inside every pair): a decision costs **D 4.08 us plus k times T 1.21 us**
for k world ticks at 16x16 and the fast scope (residuals 0.15 us), so the ceiling as k grows is **824,000 ticks a
second** (the 32x32 full k=1 baseline, 120 to 126k). 16x16 fast at k=1 is 1.56x that baseline, at k=10 it is
5.1x, at k=20 it is 5.7x. A rung buys what is left of T, and only at large k:

| k | rung 2, 16x16 fast (measured mean; D and T fitted to it) | ticks/s | rung 1, T' .95 (projection) | rung 0, T' .45 (projection) | cap |
|---|---|---|---|---|---|
| 1 | 5.18 us | 193k | 1.05x | 1.17x | 1.30x |
| 5 | 10.21 us | 490k | 1.15x | 1.60x | 2.48x |
| 10 | 16.13 us | 620k | 1.19x | 1.89x | 3.97x |
| 20 | 28.37 us | 704k | 1.23x | 2.16x | 6.93x |

The CPU at k=20 says where a rung can win: **the client tick 45%**, the server's chunk work 15%, cast and mesh
waits on the card 10%, the staging copies 6%, entities 5%, `session_tick` 4%. Rung 1 reaches only the 20% the
server holds; rung 0 reaches the client tick too, and nothing below rung 0 touches that 45%.

**A fade.** The unit is the **group**: a group is rung-homogeneous, k-homogeneous and shares one observation
shape, because a group step waits for its slowest env (`vecenv.c:3206`), the pool has one observation spec
(`pool.h:51-59`) and the trainer one `Seg` shape per group (`fastppo.py:426`). A fade rewrites `rung[g]` for a
group at its episode boundary (`group_done`, `vecenv.c:3115`) and the reset applies it; the batch is the
concatenation of the groups' segments, which already carry their own size. Four trainer-side items go with it: the
rung id as one NS scalar (the value head must separate two return regimes), `gamma ** k` per group (GAE takes one
scalar today, `fastppo.py:533`), a `stat` per rung (the running return std is one monotone Welford for the whole
run, `fastppo.py:960-975`), and the macro set masked on rung 0 for the phases that wait on a ticking server.

**The model stays a cost model.** `csrc/runtime/batchtick.c` is not wired as an env's row: it is the bench and the
target a rung-0 tick must beat (152 ns a world tick of movement, 205 to 242 ns with block interactions, 604 ns the
full tick on 32 threads, against the exact tick's 246 us). `engine.tick_scope: batched` is an alias for
`fidelity.rung: micro` until that row lands.

What the ladder needs next, in the order the measurements put them:

1. **The k-transfer A/B, before any rung-0 code** (no new code): 20.4 minutes of wall from
   `runs/fromspawn-teacher.pt` at 768 envs in 3 groups, flat at k=1 against 10.2 minutes at k=10 (`--gamma 0.980`)
   then `--init` from that model at k=1, scored by the fromspawn eval at 1,024 worlds. **If the laddered arm is
   below the flat arm outside the eval's spread (±0.054 at 3.617, `nwroot/configs/train-fast-32.yaml:5`), no rung
   below 2 is worth building**: the ladder is `fidelity.*` and `--init` chains. If it holds, rung 0 has a learning
   case and a cost case at k of 10 and up. The first attempt ran at k=1 (the vec env's one tick a decision,
   DEVLOG 2026-10-24 lane/vecticks) without `runs/pareto/recipe`. **Run with the recipe and the vec env's k**
   (DEVLOG 2026-10-24 S0b), at the density run's 512 envs in 2 groups, two seeds an arm: flat **4.108 and 2.799**,
   the ladder **3.519 and 3.761** (± 0.057 each over worlds). The ladder is not below the flat arm, but the rule's
   yardstick is wrong: ±0.054 is one model's spread over worlds, and two seeds of one recipe differ by 1.31. The
   test is the arms' difference against the seeds' standard error. At six seeds an arm (DEVLOG 2026-10-25 S0c) the
   ladder is ahead, 3.815 against 3.156 (**0.658 ± 0.295**, p 0.06), but the teacher itself scores **4.872** under
   the arms' eval (the recipe's macros with the strip): every arm lost depth to training, and on a curve of the flat
   recipe (832M decisions, a snapshot every 104M) most of it goes in the first 104M, the rest a walk as wide as the
   seeds. The ladder's lead matches its third fewer updates, so the A/B cannot price lower fidelity at this recipe.
   **Step 1 runs where the recipe climbs, from below it**: a KL anchor to the teacher (`--ref-kl` 0.1 to 1.0) keeps
   its 4.872 to 416M decisions and nothing tried raises it (DEVLOG 2026-10-25 lane/levers, levers2, levers3: levers,
   the anchor's dial, branch and select, `--recompute`); unanchored every lever falls into one band (3.4 to 4.2 by
   81k Adam steps), and two runs climbed back into it from below (2.9 to 3.9 and 2.2 to 4.1). So the ladder is
   timed from a model below the band (A2's 312M snapshot, 2.182): wall time to 3.8, k=1 against k=10 and fast,
   several seeds an arm.
2. The cross-rung gate before the rung: `pool_gate --rung R --gap NAME:ROW` holds a rung to `test_snapshots`'
   rows and passes only when the first differing row is the one its tape's manifest names (an earlier or a later
   divergence fails), on `break-place-s1` and a `s1chain-S*` segment; the frames' digests must equal too
   (`2062c91a8503b253` at 16x16 RGB).
3. Rung 0's row, then its closed-loop plumbing (the frozen chunk delivery, the chest scan, the delegated container
   ticks, the macro mask), then the per-group schedule in the trainer.
4. `fast`'s cast gate exists (`raycast-fast-gate`, lane/fastcast) and fast fails it (above); a fast that passes
   needs `cp_world_tick`'s Random draws and block reads without its particles. The maintenance switch
   (`fidelity.maintenance: step`) as part of rung 1.
5. The eval always at rung 2 (and at `full` while fast fails 4), on the fromspawn scoreboard, so a faster rung's gain
   is never an eval's own artifact.

## The environment as configuration (planned 2026-10-27)

What a run file chooses, and what each needs (GOAL.md's "done means" 2 to 5), in the order the measurements put them:

1. **Exploration, the start archive.** The vec env saves starts from its own episodes and draws them at resets
   (lane/ownstarts, lane/msstarts: cells are milestones); lane/cells adds Go-Explore's cell: the dimension, the feet's
   y in 16-block bands, and each item kind held with its count as 1, 2 or 3 and more, drawn by 1 / sqrt(1 + draws).
   Milestone cells stall at the iron pickaxe (one ingot and three are one cell). The pool is bounded by the images'
   address window (`image.h` IMG_SLOTS 1088 less the envs: 558 at 512 envs) and by memory (about 25 MB resident a
   world at spawn). Because the sim is exact, a cell can also be its start and its action tape, replayed to return
   (sim time instead of memory). To come: `explore.archive: off|milestones|cells`, `explore.pool`, `explore.share`.
2. **Actions.** Today one fixed action (`vecenv.h` NETH_VEC_ACTION 16: two 41-bin camera heads and MCSR's 14 keys, two
   of them dead in 1.7.10), and the macros start themselves on the state (`macro_start`). The plan: an `actions`
   section of the run file naming the camera (its bins; random play's camera spread moved a wooden pickaxe 9x), the
   1.7.10 keys, the hotbar and the inventory key, the slot pointer over `rlgui`'s screen slots, and the mods; each
   mod one action of a categorical head, masked by its start condition (what the player sees) and started only when
   chosen. The vec env exports the heads' sizes and a mask buffer, the trainer builds its heads from them, and a mod
   is one C file (its condition, its drive, its mask) and one table line. First step landed (lane/picks, API 10):
   the craft macro's recipes as one masked pick head (`neth_vec_opts.picks`, `bufs.picks` and `bufs.mask`).
3. **Seeds.** One seed or a set (`N`, `A-B`, `A,B,...`) in the trainer's run config (`fastppo.py --seeds`,
   mcsr-speedrun's `mcsr/seedset.py`), not a `pipeline.seeds` key: a sim key is also a key of the Java oracle's
   registry (WorldConf.java), an oracle change for a setting the oracle never reads. Each seed's start is the oracle's
   fresh world at its first tick, captured once with the run config's world section (`make -C oracle script SEED=N
   SCRIPT=tests/fresh-play-s1.jsonl`, 5.7 MB a seed, under out/java/try/seedset-HASH/) and drawn as an equal mix with
   NETH_RL_ANYSEED (without it a seed-2 start is refused in a seed-42 config). A thousand seeds load a window at a time
   (lane/seedwin, `neth_vec_opts.start_window` and `start_stride`, `fastppo --seed-window W:S`): a group's r-th
   round draws from W of the mix's starts, the window moving S a round, only the rounds in flight's starts loaded
   (about 50 MB each; all of a thousand would be about 50 GB), the next round's in the background. Made natively
   (world creation and the spawn search in C) later, gated against the captures.
4. **Run to tape.** The vec env keeps each env's acts since its start (a saved start's own prefix included), so any
   episode is written as a start and a tape the Java client replays (`make -C oracle check TAPE=...`); one found on
   a fast tier is replayed on the exact tier first.
5. **Observation parity.** The ray cast (`csrc/runtime/raycast.c`, about 2,000 lines with its sheet) ported into the
   oracle's harness to draw from the Java client world, its frame digests held to C's on the gate recordings. A
   policy playing the real client live needs it; a found run's tape replays without it.
6. **The trainer, native** (decided 2026-10-29). C and CUDA under `csrc/runtime/train/` (the repo's languages; the
   runtime is Linux and the dev host's like the rest of csrc/runtime), one binary driving libneth's vector env
   directly, reading the run file (worldconf.c's parser grows the policy part), writing the same episodes.jsonl and
   iters.jsonl so every report keeps working. In order, each with its gate:
   a. The loop and the actor: groups pipelined (launch a group's cast and actor on a stream, take the next group,
      send the first when its event fires; the main thread never blocks on one group), the net's forward and the
      sampling as fused kernels over the raw cast bytes. Gate: the actor's outputs equal the reference's on stored
      batches (fp32), and env-steps/s within 3% of vec_bench on the same sim (the env alone).
   b. The learner: forward and backward over minibatches, GAE, the clipped loss, value and entropy terms, Adam,
      fused kernels (cuBLAS where it measures faster). Gate: loss, every gradient and one Adam step against
      fastppo.py's on stored segments (fp32, a stated tolerance).
   c. End to end. Gate: G3's recipe on three trainer seeds, 25 minutes each, the milestone curves within the
      reference's spread.
   d. Then the levers lane/envprof and lane/rcab measured: the cast on the device pipelined (today neth_vec_send
      waits on it, 80% of a C loop's main thread), the client's display ticks at train-fast fidelity (14% of the env).

## The macros as mods (lane/macromod, 2026-11-05)

The env core (`csrc/runtime/vecenv.c`: the make, the raw action's decode, the step, the reward, the scalars, the
episodes, the starts) calls the macros only through `csrc/runtime/vecmods.h`'s dispatcher (`mac_*`). The macros are
`csrc/mods/macros/`, a file a macro; `macros.c` there is the dispatcher over one registration table.

- **The API** (`macro.h`): a macro fills one `struct vec_macro`: its name, its switch (a NETH_VEC_* bit in vecenv.h),
  its run-file names (`.sw`: actions.macros' words, its own bit's and any switch of its own; ntloop and brutetab read
  them through `neth_vec_macro_switch`) and the hooks of the phases it takes part in, each testing its own switch:
  `observe` (books and the step's reads, every step; a veto of the policy's act), `cand` (it could take the step),
  `aid` and `policy` (the policy's own act changed), `interrupt` (a run cut short), `start` (it took the step),
  `drive` (-1 not its run, else 1 when over), `label` and `half` (the distiller's label), `after` (a hand-back to the
  policy), `end`, `abort`, `reset`, and the books and label extras (`books`, `decision`, `fits`, `chance`, `envpick`,
  `mask`, `make`). Its state between steps is its fields of `struct vec_env` (its own struct where it has one). Its
  run-file options (actions.NAME: WORD) are lines of `csrc/runtime/vecopts.c`'s one table: their indices are
  neth_vec_opts.macro_opts' layout, which the trainer and the cases pass.
- **The table** (`macros.c` MACROS): a line a macro, its rank in each ordered phase (start, cand, observe, drive,
  label; 0 not in it). The ranks are the code's orders before the move, kept exactly: the start a priority; the
  candidate test ordered because the stronghold latches its start as it tests; the strip's lava check before the
  Nether's books; the label naming the strip while the seek it handed off to drives.
- **Adding a macro**: its file (hooks and `static const struct vec_macro MAC_NAME`), its `#include` and table line in
  `macros.c`, its bit in vecenv.h. Off by default (its bit unset), every pinned digest holds.
- **Without the macros**: `make -C csrc/runtime rlbind MACROS=0` builds `out/var/nomacros/libneth.so` with
  `csrc/runtime/nomacros.c` (no macro registered) in place of the folder: raw controls only (the keys, the camera's
  bins, the hotbar element, actions.gui cursor's clicks and inventory key), a death respawned by the env. Its digests
  equal the full library's at `--macros 0`.
- **One translation unit**: the folder compiles into vecenv.c's, every function static as before, so no call crosses a
  library boundary and the move kept every trajectory. The proof of a change here: `make -C csrc/runtime
  macro-digests` (`macrodig.tsv`: 18 rows from starts where each family runs, with options, levels and labellers:
  trajectory, label, episode and books digests) and gates-env.
