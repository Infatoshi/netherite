# netherite-v2 SPEC

Goal: a native (C, then GPU where measured to pay) reimplementation of
Minecraft 1.7.10 that is state-exact against the real Java game, fast enough
for batched RL, and playable. Same product as v1, rebuilt on a smaller game
with one tick implementation and gates that cannot pass while wrong.

## Decisions

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

Order of work (D1 is taken): the mirror rule and L16's repair; the device
renderer; the client light on the device; generation served ahead from
host threads; tick kernels behind the win gate (S6's random ticks first,
S11, the client and the network last or never: they change daily).
