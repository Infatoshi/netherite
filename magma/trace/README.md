# Tick-Trace Divergence Oracle

> Verification entry point: `magma/VERIFY.md`. Day-to-day input replay is
> `../verify/trace/replay_tape.py`; this dir keeps the headless physics-only
> C tracer (`trace_game`) it superseded.

Run a **fixed action tape** through BOTH the real Java Minecraft 1.11.2 game (ground
truth) and the magma C game, capture per-tick state, and find the **first tick where
they diverge** in physics and in pixels - while staying disk-efficient (never a frame
per tick; two small CSVs per run, frames materialized only at a divergence).

This is the bug-finding flywheel: the C reimplementation is driven by the SAME discrete
action space (qrl) as the Java game, so any behavioural gap shows up as a concrete
tick + field + magnitude.

## Files

- `../PARITY_PROJECT.md` - ordered full-parity backlog, acceptance tests, and
  performance budgets.
- `gen_tape.py` - write a deterministic action tape (seeded pseudo-random walk).
- `trace_java.py` - replay the tape through the REAL game via the qrl bridge -> `java_phys.csv`.
- `../app/trace_main.c` + `build_c_tracer.sh` - the narrow headless C physics
  tracer (`../trace_game`) -> `c_phys.csv` + `c_state_small.jsonl`.
- `trace_runtime.py` - drive the shipped shared `GmRuntime` with the same tape
  and normalize its state -> the authoritative `c_state.jsonl`.
- `diff_trace.py` - align the two CSVs, report the first divergence + per-field summary,
  and (on `--materialize`) dump only the frames around the divergence tick.
- `block_diff.py` - strict raw block id+metadata comparison at tick N, plus a
  pre-to-post transition view that separates baseline worldgen from simulation
  outcomes. Java and the shipped full `GmRuntime` both start from the capsule's
  packed cuboid; `trace_game` is no longer used as the active-world result.
- `light_diff.py` - strict one-byte-per-cell block- or sky-light comparison,
  including coordinate diagnostics, mismatch CSV, and
  identity/value-negative controls.
- `state_capsule.py` - validate, normalize, checksum, and deterministically
  replay the neutral save-state contract shared by Java and magma.
- `run_oracle_matrix.py` - strict multi-seed state/block regression matrix over
  isolated Java clients. Local infrastructure failures recycle only the bad
  numbered client and retry its case once.
- Current correctness promotion: 549/549 behavior/raw outcomes, with 545 strict
  state passes and four explicitly diagnostic delayed cactus trajectories. The
  four-client sweep is at
  `out/matrix_redstone_piston_farmland_grass_path_shape_full_1/summary.md`; its
  two contaminated older fixtures and all four new rows pass after case-local
  correction at
  `out/redstone_piston_farmland_grass_path_shape_fixture_hardening_1/summary.md`.
  The current performance guard passes at 4,971 scalar steps/s, 2.93M Blaze
  env-ticks/s, and 29.56 CUDA fps on GPU 1 at
  `out/perf_guard_live_blaze_entities_gamerule_1.json`. GPUs 2 and 3 were
  saturated by unrelated jobs during this measurement; all metrics remain
  above their machine-local regression floors.
- `../raster/verify/scenarios/double_plant_gallery.yaml` - six-species real-Java
  structural pixel fixture. Its 2026-08-02 promotion replays 1,188 exact
  physics/world ticks and passes the pixel gate over 119 frames.
- `../raster/verify/scenarios/blaze_attack_cycle.yaml` - idle real-Java blaze
  cycle anchor. The steady cycle in the 2026-08-02 capture exposes the exact
  charge, three-shot, and clear sequence; its 41-frame structural pixel gate
  passes. Live C cadence, damage, motion, continuous expanded-player-AABB ray,
  and lower-slab pass-through assertions are in `game/test_mob_live.c`.
- `run_oracle_matrix_pool.sh` - on-demand 1-32-client matrix wrapper with exact
  parallel cleanup, a 180-second case bound, and the 300/350 GiB oracle cgroup.
  Promotions currently use four clients. Higher counts can race in shared
  ForgeGradle schema/classpath tasks, so they are not the active feature path.
- `run_moving_piston_checkpoint_regression.sh` - three-case real-Java capture
  plus exact mid-motion capsule resume/progress/settlement comparison.
- `perf_guard.py` + `perf_baseline_gpu1.json` - machine-local median
  performance regression gate.
- `out/` - all artifacts (gitignore-able): `tape.txt`, `c_phys.csv`, `java_phys.csv`,
  `*_blocks_before.bin`, `*_blocks.bin`, `*_light*.bin`,
  `block_mismatches.csv`, `*_light_mismatches.csv`, `diverge_<tick>/`.

## Action tape format

Plain text, one line per tick, whitespace-separated integers in the qrl action order:

```
forward back left right jump sneak sprint attack use yaw pitch
```

`forward/back/left/right/jump/sneak/sprint/attack/use` in `{0,1}`; `yaw/pitch` in
`{-1,0,1}` = 15-degree quantum aim steps (matches `java/qrl_client.py` and
`Recorder.applyAction`). `#` comments and blank lines are ignored. Both tracers
consume the exact same file, so the input is identical on both sides.

## Trace artifacts

The oracle emits:

1. **Legacy physics CSV** (`c_phys.csv` / `java_phys.csv`) - unchanged, for back-compat with
   `frame_oracle.py` / `world_diff.py`:
   ```
   tick,x,y,z,yaw,pitch,vx,vy,vz,on_ground,health,food,air,frame_hash
   ```
2. **Full STATE VECTOR JSONL** (`c_state.jsonl` / `java_state.jsonl`) - the
   per-tick, per-feature diff target. The C side comes from the same `GmRuntime`
   used by interactive and headless play. `c_state_small.jsonl` preserves the
   narrow player-only tracer as a diagnostic.

## State-vector schema (`*_state.jsonl`, identical keys both sides)

One JSON object per line:

```json
{"tick":0,
 "world_rng":{"java_seed48":25214903908,"math_seed48":188900966474565,
              "block_seed48":11718085204285,"update_lcg":1094913777},
 "controlled_input":{
   "before":{"entity_id_cursor":100,
              "world_rng":{"java_seed48":0,"math_seed48":1,
                           "block_seed48":2,"update_lcg":3}},
   "entity_id_cursor":101,
   "world_rng":{"java_seed48":205749139540596,"math_seed48":1,
                "block_seed48":2,"update_lcg":3}},
 "player":{
   "x","y","z","yaw","pitch","vx","vy","vz","on_ground",
   "health","food","saturation","air","fire","xp_level","xp_frac","fall_distance",
   "sprinting","sneaking","jumping",
   "held_slot","held_id","held_count","held_meta",
   "attack_cooldown","hurt_time","death_time","dead","deaths","dim","potions"},
 "inventory":[{"slot","id","count","meta"}, ...],
 "entities":[{"eid","type","x","y","z","dx","dy","dz","vx","vy","vz",
              "yaw","pitch","health","no_ai","hurt_time","death_time",
              "hurt_resistant_time","item","count","meta","value","age",
              "pickup_delay","color","target_color"}, ...],
 "scheduled_ticks":[{"x","y","z","block","time","priority","order"}, ...],
 "time":{"world_time","total_time","moon_phase","raining","thundering"}}
```

**`null` = UNSUPPORTED/UNOBSERVED in the full C runtime (a SENTINEL, not a
value).** Those fields are reported as UNSIMULATED rather than "matching zero".

`controlled_input` is non-null only on a row with a tick-boundary block edit.
It brackets the edit and synchronously drained block events before unrelated
server work can contaminate the result. If Java and C have the same `before`
bundle, the comparator requires the complete absolute bundle to match. If a
later edit begins after Java-only loaded-chunk/client work changed the absolute
start, `controlled_input.causal` compares the exact number of raw 48-bit LCG
transitions for `World.rand`, `Math.random`, and `Block.RANDOM`, plus the
entity-ID delta and modulo-32-bit `updateLCG` delta. The comparison is bounded
at one million raw LCG transitions per cursor/edit; exceeding that bound is a
failure, not an inferred match.

- **Simulated/exposed on C**: player physics, look, vitals, fire ticks, XP,
  fall distance, flags, held item and inventory, hurt timer, death state,
  bounded active-potion state, live entities, and time/weather. Speed and
  Slowness apply their exact movement-speed attribute modifiers; mob-applied
  Wither remains exposed through the same canonical potion list.
- **UNSIMULATED on C -> `null`**: player death timer. Attack cooldown is a
  modeled value and is gated in movement, mining, and melee fixtures.
  Periodic potion actions, particles, brewing, and the remainder of the effect
  catalog remain feature backlog items.

**Inventory id caveat**: Java uses vanilla registry ids (`Item.getIdFromItem`), C uses blaze
`IC_*` ids - a DIFFERENT namespace. The diff reports inventory occupancy + counts + full-tuple
separately and flags the id-namespace mismatch, so "ids differ" is not mistaken for a bug.

## Legacy CSV columns

`x/y/z` = world FEET coords, `yaw/pitch` = MC-convention degrees, `vx/vy/vz` = motion,
`on_ground/health/food/air` are simulated scalars, and `frame_hash` is the
64-bit FNV-1a of the rendered RGBA framebuffer (C only; the Java side writes 0
because frames are not grabbed per tick).

## Spawn alignment (do this FIRST or the diff is meaningless)

The C tracer spawns at the magma worldgen origin column; Java spawns at the REAL
world spawn. The C tracer writes its exact settled tick-0 travel state to
`c_spawn.txt` (`X Y Z YAW PITCH VX VY VZ ON_GROUND FALL_DISTANCE`, world feet /
MC degrees). `trace_java.py` consumes the first five fields and settles its own
teleport; `trace_runtime.py` loads all ten directly. Both sides therefore start
from the same pre-tick state. `run_oracle.sh` wires this automatically. (Fix landed here too: the
C tracer now rebuilds the collision AABB after repositioning the spawn - previously it set
`posY` but not the box, so physics snapped back to the init Y and the spawn was silently
ignored.)

## One command

```bash
cd magma && bash trace/run_oracle.sh          # TICKS=300 SEED=0 by default
```

Builds the tracer, gens the tape, runs C, and - if the qrl bridge is up on `:25575` -
spawn-aligns + runs Java + prints the per-feature and raw-block diffs. If the bridge is
down it prints state and block self-diffs. `PLATFORM=N` (default 21) stages the same
stone pad plus six cleared headroom layers in both engines and verifies the Java
support cell from the server; `PLATFORM=0` uses raw terrain (Java may free-fall).
`FRESH=1` is also the default: Java deletes and regenerates `qrl_<seed>` before each
trace, then waits until frozen time 6000 is visible client-side. `FRESH=0` is reserved
for deliberate save-reload/evolved-world tests. Both inventories are cleared before
tick 0 so held-item state is not an artificial harness mismatch. The Java tracer
arms a bounded exact-step mode and aborts unless both the client player and
integrated server player advance exactly one tick for every tape row. The
server parks at `ServerTickEvent.START`, consumes one permit, publishes an
authoritative snapshot at `END`, and parks again before the next action. The
client thread waits for that permitted server tick to finish before it can
emit the newly installed action's packet. This makes the intended one-tick
client-to-server packet delay deterministic rather than thread-scheduling
dependent. The parked pre-tick normalization also clears queued physical key
presses and unsaved movement/block-dig cursors when a client process is reused.

The hidden-state checkpoint continuation also has a one-command regression:

```bash
cd magma
QRL_PORT=25600 bash trace/run_torch_checkpoint_regression.sh
```

It captures Java after seven redstone-torch toggles, proves the checkpoint's
visible blocks and light are unchanged, restores the invisible chronological
toggle history into magma, applies only the eighth cycle, and compares the
continued state, queue, raw blocks, and all sampled block-light cells.

For cold potion fixtures only, the client movement attribute is rebuilt from
the parked authoritative server snapshot at `ClientTickEvent.START`, after
the game loop drains scheduled property-packet tasks and before the next
client movement tick. This controls packet-delivery jitter while retaining
server-first effect aging.
Java natural spawning is disabled and non-player entities are cleared for the
base state gate; entity parity is tested with explicit fixtures.
`diff_trace.py` defaults to a 16-block player-relative entity radius so distant
chunk-population side effects do not contaminate a local scenario.
Authoritative server snapshots include non-player entity pose, motion, health,
and XP-orb fields. `XP_ORB_FIXTURE="DX DY DZ VALUE"` asks the parked Java
server to create one exact orb, records its real entity ID and pre-tick state,
including its hidden target-refresh cursor, and cold-loads the same fixture
into `GmRuntime`. `MOB_FIXTURE="DX DY DZ HEALTH"` similarly creates a locked
NoAI target for physical-click/cooldown/hurt/death timing. An optional fifth
literal, `collision`, instead keeps Java AI enabled while clearing the pig's
tasks, disabling gravity, and setting movement speed to zero. That fixture
remains stationary but follows vanilla's ordinary `move(0,0,0)` to
`doBlockCollisions` path; magma records the same entity as a controlled mover.
`ITEM_FIXTURE="DX DY DZ ITEM COUNT META"` creates a real gravity-free,
zero-motion `EntityItem` with pickup delay 32767, records its authoritative
EID, stack, pose/motion, age, and delay, and replays that exact sidecar in
magma. Its `controlled_stationary` marker suppresses magma's approximate
ground-item gravity while preserving ordinary per-tick aging and block
collision checks.
`ARROW_FIXTURE="DX DY DZ [FIRE_SECONDS]"` creates an exact gravity-free,
zero-motion
`EntityTippedArrow`, records its authoritative EID and pose, and restores it
in magma's fixed projectile pool. With the optional fire duration, the sidecar
also records exact fire ticks and the oracle can pin constructor cursors at an
armed burning-arrow/TNT contact. It is used to query wooden-button selection
AABBs and TNT collision behavior without projectile-flight or gravity noise.
`PRIMED_TNT_FIXTURE="DX DY DZ VX VY VZ FUSE"` restores one exact
`EntityTNTPrimed` save-state row, including authoritative EID, motion, and
fuse. Its oracle control pins `World.rand` only at armed fuse-zero detonation,
so unrelated same-tick world work cannot contaminate a narrow explosion proof.
`PLATFORM_CLEAR_HEIGHT` defaults to 6 and may widen staged vertical headroom
for an isolated high explosion fixture.
`POTION_FIXTURE="ID AMPLIFIER DURATION"` clears/drains prior effect packets,
seeds the exact server/client effect state at the parked boundary, and records
both movement-speed attributes as diagnostics. The matrix's Speed II fixture
requires duration 5..1, removal on tick 5, and an exact derived movement
transition on both engines.
`SCHEDULE_TICK_FIXTURE="DX DY DZ BLOCK DELAY PRIORITY [CALLBACK_SEED]"`
inserts one real pending update at the parked boundary. With the optional seed,
the fixture first replaces a placement-created update for that cell, then
reseeds Java immediately before the world tick that reaches its due time;
magma applies the same internal 48-bit cursor before its corresponding
dispatch. This isolates scheduled callback/order semantics from unrelated
intervening `World.rand` consumers.
`SCHEDULE_CAPTURE_BLOCKS="ID [ID ...]"` additionally includes naturally
created pending entries for those block IDs in the canonical Java queue. It is
used when a real neighbor callback creates work during the tape rather than
when the harness seeds that work before tick 0.
`RANDOM_TICK_FIXTURE="DX DY DZ BLOCK SEED"` seeds `World.rand` while the server
is parked, records its private 48-bit `java.util.Random` cursor in the capsule,
and queues one real `Block.randomTick` callback on the next server tick thread.
Magma restores that cursor and invokes the represented callback before the
same tape tick. This fixture tests callback semantics; it deliberately does
not bypass or claim parity for `WorldServer` loaded-chunk selection.
`RANDOM_SELECTION_FIXTURE="DX DY DZ BLOCK SEED"` is the complementary natural
selector proof. While parked, Java replaces every other random-ticking block
in the actual loaded chunk collection with inert stone, verifies exactly one
eligible section/block, and promotes the target's already-loaded player-chunk
entry to rank zero so lazy iterator membership cannot race the preview. It
chooses a pre-tick `updateLCG` whose next natural selection lands on the
target. The promotion, exact rank, and count of LCG advances are written to a
sidecar and replayed by magma. The ordinary
`WorldServer.updateBlocks` loop—not the bridge—invokes the Java callback.
`TICK0_BLOCK_FIXTURE="DX DY DZ BLOCK META"` queues one real
`WorldServer.setBlockState` on the server thread immediately after the tape
tick-0 permit. `WORLD_RANDOM_SEED48` and `BLOCK_RANDOM_SEED48` optionally set
the corresponding internal 48-bit cursors while the server is parked; values
are internal `AtomicLong` payloads in `0..2^48-1`, not public constructor
seeds. `trace_java.py` exposes the same controls as `--world-random-seed48`
and `--block-random-seed48`. The capsule captures those seeded prestates and
replays them into the C runtime. `trace_java.py` can dump parked pre/post
`EnumSkyBlock.BLOCK` and `EnumSkyBlock.SKY` cuboids; `trace_runtime.py`
exports magma's corresponding raw light values after the same script event.
For skylight comparisons the exact Java pre-state nibble field is also placed
in the capsule, so the mutation starts from saved-state identity rather than a
separately reconstructed light field.

## Post-tick raw block outcome gate

Both tracers sample the same inclusive `BLOCK_BOX` before tick 0 and after tick N.
The format is exactly qrl `getblocks`: headerless little-endian `u16`,
`numeric_block_id << 4 | metadata`, serialized y-major then z then x.
`block_diff.py` reports two deliberately separate contracts:

1. **Final state** compares every sampled cell. This exposes worldgen and simulation
   differences together.
2. **Transition state** compares tick-N results only at coordinates whose tick-0
   state matched. This prevents a misplaced tree at baseline from masquerading as
   a block-tick or player-edit bug. It also reports every mutation and whether its
   baseline was shared.

The default random walk is diagnostic and may observe no mutations. This controlled
mining gate is non-vacuous: both engines receive the same stone target, hold attack
past the bare-hand hardness threshold, and must perform the same shared-baseline edit.

```bash
cd magma
TICKS=180 \
TAPE_PROFILE=block-break \
FIXTURE_BLOCK="8 79 6 1 0" \
BLOCK_STRICT=transition \
bash trace/run_oracle.sh
```

Useful controls:

- `BLOCK_BOX="-4 -6 -4 20 10 20"` selects the default player-relative cuboid.
  Set `BLOCK_BOX_RELATIVE_Y=0` when supplying absolute Y bounds explicitly.
- `BLOCK_STRICT=0` prints differences without failing, `1` requires identical final
  worlds, and `transition` requires identical results on the shared baseline.
- `REQUIRE_BLOCK_MUTATION=1` rejects a vacuous transition pass. It defaults to 1 for
  the `block-break` profile.
- `uv run --no-project python trace/block_diff.py --selftest` runs identity plus
  block-id, metadata, baseline-split, and transition negative controls.
- `uv run --no-project python trace/light_diff.py --selftest` runs exact
  identity and a one-cell changed-light negative control.

For a live light-propagation comparison:

```bash
cd magma
TICKS=1 TAPE_PROFILE=idle \
TICK0_BLOCK_FIXTURE="4 1 0 89 0" \
BLOCK_STRICT=transition REQUIRE_BLOCK_MUTATION=1 \
bash trace/run_oracle.sh
```

The addition matrix case requires the source cell to change from block light 0
to 15, at least one direct neighbor to be 14, one air-to-glowstone raw block
mutation, and exact equality across all 10,625 sampled light cells. Its
complementary final-stage fixture starts with that source in the capsule,
removes it at tick 0, and requires the 15/14 field to drain completely to zero
on both engines. A third case inserts opaque stone beside the source and
requires the blocker to change 14-to-0 and the cell behind it to reroute
13-to-11, again with full-cuboid equality. A fourth inserts a second glowstone
four blocks away and requires its cell to change 11-to-15, the merge edge to
change 12-to-14, and exactly 1,304 light cells to change identically. A fifth
places the emitter at local x=15 and requires its immediate next-chunk x=16
neighbor to become light 14, with 2,342 changed light cells exact.

## Parallel oracle matrix

`java/start_oracle_instance.sh` runs isolated headless Java clients for concurrent
comparisons. Each instance gets a distinct X display, QRL port, username, game
directory, save directory, and log directory. The game-directory isolation is
essential: a fresh reset deletes `qrl_<seed>`, so clients must never share `run/saves`.
VNC is optional (`ORACLE_POOL_VNC=1`); ordinary matrix runs use CPU llvmpipe.

```bash
# Start two persistent clients (ports 25600 and 25601).
java/start_oracle_instance.sh start 0 101
java/start_oracle_instance.sh start 1 102

# Build the C tracer once, then schedule the default seed-0/seed-1 random walks,
# strict non-vacuous mining; scheduled stone, water, lava, reaction, isolated
# natural cactus selection, wheat callback, lighting, and falling-sand cases;
# drowning/surface packet cases; XP, melee, potion expiry, and fire.
cd magma
uv run --no-project python trace/run_oracle_matrix.py --instances 2
```

When another long-running Gradle client owns the default cache, set
`ORACLE_GRADLE_CACHE` and optionally `ORACLE_PROJECT_CACHE` to isolated,
pre-populated directories for each oracle instead of stopping unrelated
clients.

The runner records each case under `trace/out/matrix/<case>/` and writes
`summary.json` plus `summary.md`. Every default case requires zero divergence
across observed state features as well as the strict shared-baseline block
transition. Mining also requires a non-vacuous mutation. Survival cases have
behavior gates, so a trace cannot pass merely because both engines failed to
enter the target state. The focused 25-tick surface case specifically gates
the integrated-server movement boundary: server `onGround` remains true while
the client begins rising, then the delayed position packet produces Java's
exact jump plus movement exhaustion. Fire gates cover raw counter decay, wet
extinguishing, and real block contact. Exit status is 4 for a parity failure
and 1 for an infrastructure failure. The XP gate keeps a value-5 orb visible
for ticks 0 through 7, requires exact entity state, then checks its tick-8
removal and the simultaneous 5/7 level-fraction award. The melee gate checks a
partial rejected hit, fully cooled accepted hits, exhaustion, hurt/death timer
decay, and removal at death time 20. The scheduled-stone gate checks absolute
due time and queue drain. Water gates check the first active source dispatch,
two flat-plane generations, one-block falling metadata, source wakeup, Java's
exact 5/12-entry and 2/10-entry queue orders, and every required raw mutation.
Lava gates cover its natural 30-tick Overworld cadence, two flat generations,
level-2/4 metadata, a still source, and downward water-to-stone reaction with
the exact source requeue. The falling-sand gate checks scheduled dispatch,
nine exact transient-entity states, landing, the +2 stability update, and both
raw block mutations. The crop gate restores `Random(seed=9)`, invokes the real
wheat callback, and requires exactly one age-0-to-age-1 metadata mutation on
both engines. The isolated natural-selector gate verifies one eligible loaded
section, explicitly promotes its already-loaded player-chunk entry to rank
zero, derives the same target from `updateLCG`, and requires exactly one
cactus age-0-to-age-1 mutation. This keeps the selector proof independent of
the lazy iterator's transient later membership and of live light state.
The pressure-plate lifecycle gates separately prove capsule-restored
unoccupied release, a live forward walkover, a player remaining inside the
trigger AABB through the first due callback, and a stationary living pig
activating through its ordinary entity collision pass and remaining through
the same recurrence. A fifth fixture proves wooden `Sensitivity.EVERYTHING`:
a stationary dropped item activates block 72 and renews its +20 callback,
while the native negative leaves stone block 70 unpowered under an identical
item. Three weighted fixtures complete the family: player plus one item maps
to gold strength/dust 2 and iron strength/dust 1 using the exact ceiling
formula, both renew at +10, and a saved gold strength-2 callback releases the
plate and dust to zero before the lamp's independent +4 drain. The eight cases
compare every queue observation, exact entity state, exact plate/dust/lamp
mutations, and all 10,625 sampled block-light cells. Occupied cases require
the replacement callback rather than accepting a merely powered final block.
A true NoAI pig remains the unit-level non-activation control because vanilla
skips its movement/block-collision path; a count-64 item stack separately
proves weighted plates count entities rather than stack items.
The button callback gate covers all six attached-support directions. Four
short saved callbacks supplement the original floor and east-wall cases,
requiring metadata 8..13 to release to 0..5 without changing orientation,
notifying the correct support, and reproducing the independent +4 lamp
handoff with exact raw light. Wooden buttons add two cases: an exact stationary
arrow activates floor metadata 5 to 13, retains and replaces the callback at
the vanilla +30 cadence, and leaves the arrow exact for all 32 observations;
an arrow-free saved callback releases 13 to 5 at +3 before the lamp's +4
drain. Native controls prove the identical arrow does not activate stone.

The repeater gate covers unpowered/powered IDs 93/94, every horizontal
orientation, and delay metadata 1/5/9/13. Rising edges queue at exact
+2/+4/+6/+8 with priority -1, falling edges use priority -2, and a repeater
chain proves the upstream priority -3 rule before the downstream -1 callback.
A one-observation input proves the forced minimum pulse, while a perpendicular
powered repeater and its removal prove lock and unlock notification behavior.
Direct output, strong output through an ordinary stone block, and the lamp's
independent +4 drain are raw-block and raw-light exact. Three capsule cases
restore block 93 power-on, block 94 power-off, and a no-input minimum pulse
with their absolute due time, priority, and tie-break order. The focused
16-case family is in
`trace/out/matrix_redstone_repeater_family_1/summary.md`; the 100-case
aggregate is in `trace/out/matrix_redstone_repeater_full_1/summary.md`.

The comparator gate covers unpowered/powered IDs 149/150, every horizontal
orientation, compare/subtract metadata, rear analog input, raw side-dust
strength, directional weak/strong output, and the exact two-tick callback.
Normal transitions use priority 0; a diode in the output topology selects
priority -1. Threshold fixtures prove compare inputs below, equal to, and
above the rear strength, plus subtract outputs 2/0/0. They deliberately retain
the vanilla edge states where compare rear 7/side 8 stores tile output 7 while
remaining unpowered and subtract rear 7/side 7 accepts a callback without
changing its zero output. Three saved-state cases restore output 0, output 15,
and the comparator-to-repeater priority chain with absolute due time,
priority, and order. Oracle state includes the sorted nearby
`TileEntityComparator` list, so a matching block without matching analog tile
state cannot pass. The focused evidence is split across
`trace/out/redstone_comparator_analog_family_1/summary.md`,
`trace/out/redstone_comparator_directions_1/summary.md`, and
`trace/out/redstone_comparator_repeater_priority_fix_1/summary.md`; the
114-case aggregate is in
`trace/out/matrix_redstone_comparator_full_1/summary.md`.

Static comparator overrides add cake output `(7-bites)*2`, cauldron level,
End-frame eye 0/15, and vanilla's one-normal-cube look-through. Override
edits execute the bounded horizontal `updateComparatorOutputLevel` scan, so a
comparator directly adjacent or exactly one normal cube away receives the
same notification without any per-tick world scan. A saved cauldron-through-
stone case restores the second input block, output-signal zero, and pending
callback before both engines commit output 3. The focused 19-case comparator
family is in
`trace/out/matrix_redstone_comparator_static_family_1/summary.md`; the
119-case aggregate is in
`trace/out/matrix_redstone_comparator_static_full_1/summary.md`.

The first inventory-backed override covers an ordinary 27-slot single chest.
Its saved-state fixture restores one full stone stack in slot 0, comparator
tile output zero, and a callback due at +3. Java's exact fullness formula
therefore emits strength 1; both engines commit that output and power the lamp
on the same observation while preserving all 27 slots. The clean unfixed
behavior probe is in
`trace/out/redstone_comparator_saved_single_chest_probe_1/summary.md`.
The first behavioral fix exposed an independent 13-cell light mismatch in
`trace/out/redstone_comparator_saved_single_chest_fix_1/summary.md`: block 54
is non-opaque in Java, not opacity 255. The corrected focused result is in
`trace/out/redstone_comparator_saved_single_chest_fix_2/summary.md`, the
20-case comparator family is in
`trace/out/matrix_redstone_comparator_chest_family_1/summary.md`, and the
120-case aggregate is in
`trace/out/matrix_redstone_comparator_single_chest_full_1/summary.md`.

Furnaces are the next exact inventory override. The canonical tile payload
contains all three sparse slots plus burn time, current burn time, cook time,
and total cook time. A saved unlit furnace with one full stone stack in its
input slot emits strength 5. The deliberate omission control at
`trace/out/redstone_comparator_saved_furnace_probe_1/summary.md` matches the
furnace and callback state but first diverges on comparator output at
observation 1, followed by exactly the comparator/lamp cells. The corrected
focused result is in
`trace/out/redstone_comparator_saved_furnace_fix_1/summary.md`, the 21-case
comparator family is in
`trace/out/matrix_redstone_comparator_furnace_family_1/summary.md`, and the
121-case aggregate is in
`trace/out/matrix_redstone_comparator_furnace_full_1/summary.md`.

Ordinary double chests are represented as two reciprocal 27-slot tile
payloads and evaluated as one 54-slot inventory. The discriminator restores
four full stacks in one half: vanilla emits strength 2, whereas incorrectly
reading only that half emits 3. That deliberate analog-only failure is in
`trace/out/redstone_comparator_saved_double_chest_probe_1/summary.md`; both
values power the same blocks, so the raw-block gate passes while the
comparator tile-state and semantic gates correctly fail at observation 1.
The fixed focused result is in
`trace/out/redstone_comparator_saved_double_chest_fix_1/summary.md`, the
22-case family is in
`trace/out/matrix_redstone_comparator_double_chest_family_1/summary.md`, and
the 122-case aggregate is in
`trace/out/matrix_redstone_comparator_double_chest_full_1/summary.md`.

Closed single trapped chests use the same 27-slot comparator-fullness formula
while retaining block ID 146 and a distinct capsule schema. The exact boundary
requires air above, no adjacent trapped-chest half, `numPlayersUsing=0`, and
zero lid motion.
The clean omission control at
`trace/out/redstone_comparator_saved_single_trapped_chest_probe_1/summary.md`
matches container and scheduled state, then first diverges at observation 1
with Java output 1 and Magma output 0. The corrected focused result is in
`trace/out/redstone_comparator_saved_single_trapped_chest_fix_1/summary.md`,
the 23-case family is in
`trace/out/matrix_redstone_comparator_trapped_chest_family_1/summary.md`, and
the promoted 123-case aggregate for that slice is in
`trace/out/matrix_redstone_comparator_trapped_chest_full_2/summary.md`.

The earlier
`trace/out/matrix_redstone_comparator_trapped_chest_full_1/summary.md` is
retained as rejected harness evidence: early static-water fixture staging
could leave an uncaptured in-flight water callback, causing Java alone to
settle two water cells after the captured boundary. Final-fixture alignment
now restores exact `vx/vy/vz`, `onGround`, and `fallDistance` after locked
block staging. The wet-fire regression ends at its two-tick uncontaminated
causal boundary (immediate extinguish and no damage); longer fluid dispatch is
covered by the dedicated water cases. That result passed independently on all
four oracle instances before the aggregate was promoted.

Closed double trapped chests retain a separate
`double_trapped_chest_half` schema while composing the same reciprocal 54-slot
inventory as ordinary pairs. Four full stacks in one half distinguish output 2
from the incorrect single-half output 3. The deliberate analog-only failure is
in
`trace/out/redstone_comparator_saved_double_trapped_chest_probe_1/summary.md`;
both outputs power the same blocks, so raw block/light gates pass while the
comparator tile and semantic gates fail. The corrected focused result is in
`trace/out/redstone_comparator_saved_double_trapped_chest_fix_1/summary.md`,
the 24-case family is in
`trace/out/matrix_redstone_comparator_double_trapped_chest_family_1/summary.md`,
and the 124-case aggregate is in
`trace/out/matrix_redstone_comparator_double_trapped_chest_full_1/summary.md`.

Live trapped-chest redstone uses the `trapped-chest` tape profile: the
12-field tape adds a final `close` input, presses use at tick 2, and closes at
tick 7. Delaying the use edge gives the real client raycast a stable fixture
before its block-use packet reaches the integrated server on the next locked
tick. The promoted trace at
`trace/out/redstone_trapped_chest_viewer_power_fix_2/summary.md` asserts the
complete lifecycle rather than requiring a final mutation: viewer count is
zero through observation 2, one through 6, and zero from 7 onward; lid angle
rises from 0 to 0.4 and decays to 0; successful use resets attack cooldown;
and the direct weak lamp plus the upward-strong-through-stone lamp create two
ordered block-124 callbacks at close, retain them through observation 9, and
drain before observation 10. Both lamps intentionally finish as block 123,
byte-identical to the shared baseline. The pre-fix discriminator at
`trace/out/redstone_trapped_chest_viewer_power_probe_6/summary.md` matches
container/lid/swing timing and first diverges only at the missing two-entry
callback queue. Native regressions separately prove that ordinary chest
viewers emit no power and trapped-chest strong power does not pass
horizontally through a normal cube. The 25-case family is in
`trace/out/matrix_redstone_trapped_chest_viewer_power_family_1/summary.md`;
the promoted 125-case aggregate is in
`trace/out/matrix_redstone_trapped_chest_viewer_power_full_1/summary.md`.

Saved dispenser/dropper comparator parity uses two independent final fixtures
for block IDs 23 and 158. Oracle capture emits an exact sparse nine-slot
`dispenser` or `dropper` payload, the neutral capsule restores it before the
saved comparator callback, and Magma evaluates the same
`Container.calcRedstoneFromInventory` float formula. With one full stone stack
in slot 0, both engines commit analog output 2 at observation 1 and power the
lamp with exact queue, tile, block, and light state. The clean pre-fix
dispenser control at
`trace/out/redstone_comparator_saved_dispenser_probe_1/summary.md` retains the
same inventory and callback but leaves Magma at output 0. Corrected focused
results are in
`trace/out/redstone_comparator_saved_dispenser_fix_1/summary.md` and
`trace/out/redstone_comparator_saved_dropper_fix_1/summary.md`. Native
regressions additionally prove empty output 0, reject slot 9, and verify the
per-item limit with one non-stackable dropper item. The 27-case affected
family is in
`trace/out/matrix_redstone_comparator_dispenser_dropper_family_1/summary.md`;
the promoted 127-case aggregate is in
`trace/out/matrix_redstone_comparator_dispenser_dropper_full_1/summary.md`.

Saved jukebox comparator fixtures use the same neutral tile list with a
distinct one-slot `jukebox` schema. The exact boundary admits an empty
metadata-0 tile or one untagged vanilla record ID 2256..2267 with count 1,
metadata 0, and block metadata 1. The deliberate omission at
`trace/out/redstone_comparator_saved_jukebox_probe_1/summary.md` preserves
record 13 and the saved comparator callback but leaves Magma at output 0 when
Java commits 1. Corrected first/last-record fixtures pass at
`trace/out/redstone_comparator_saved_jukebox_fix_1/summary.md` and
`trace/out/redstone_comparator_saved_jukebox_record_wait_fix_1/summary.md`,
proving outputs 1 and 12 with exact tile, queue, block, and light state.
Native controls additionally prove empty output 0 and reject non-record
items. The 29-case family is in
`trace/out/matrix_redstone_comparator_jukebox_family_1/summary.md`; the
promoted 129-case aggregate is in
`trace/out/matrix_redstone_comparator_jukebox_full_1/summary.md`. This slice
does not claim record playback or audio parity.

Saved command-block comparator fixtures cover impulse, repeating, and chain
IDs 137/210/211. Oracle capture and the neutral capsule admit only the exact
inert defaults: empty command, name `@`, output tracking enabled, no last
output or result stats, unpowered/unmet condition state, the vanilla per-ID
`auto` value, no pending command callback, and `successCount` in 0..15.
`trace/out/redstone_comparator_saved_command_block_probe_2/summary.md`
preserves the complete command tile, comparator tile, and scheduled callback
on both engines, then first diverges at observation 1 with Java output 7 and
Magma output 0. Corrected results for all three block IDs are in
`trace/out/redstone_comparator_saved_command_blocks_fix_1/summary.md`.
Native controls also prove zero output, reject count 16, and retire the cold
tile state on replacement. The 32-case affected family is in
`trace/out/matrix_redstone_comparator_command_block_family_1/summary.md`; the
promoted 132-case aggregate is in
`trace/out/matrix_redstone_comparator_command_block_full_1/summary.md`.
This slice deliberately does not execute commands or claim their powered,
scheduled, textual-output, or result-stat lifecycle.

Saved item-frame comparator fixtures use a dedicated complete `item_frames`
list rather than the generic nearby-entity list. The exact subset admits at
most 256 frames, each with a unique entity ID and exact pose, hanging
coordinate, horizontal facing, item tuple, and rotation. A comparator may
consume a frame only when its immediate rear block is a represented normal
cube, the next block is represented air, and exactly one frame in that cell
faces the comparator direction. Empty frames emit zero; the represented
plain-stone stack emits `rotation + 1` for rotation 0..7.
`trace/out/redstone_comparator_saved_item_frame_probe_1/summary.md` preserves
the complete frame, comparator tile, and scheduled callback on both engines,
then first diverges at observation 1 with Java output 7 and Magma output 0.
The corrected result is in
`trace/out/redstone_comparator_saved_item_frame_fix_1/summary.md`. Native
controls additionally prove rotation 7 -> 8, empty -> 0, invalid rotation and
item rejection, and state retirement after support replacement. The 32-case
affected family is in
`trace/out/matrix_redstone_comparator_item_frame_family_1/summary.md`; the
promoted 133-case aggregate is in
`trace/out/matrix_redstone_comparator_item_frame_full_1/summary.md`. This
slice does not claim damage, drops, map/tag state, rendering, or general frame
lifecycle.

Observer fixtures cover block ID 218 in all six facing states. Early fixtures
use `fixture_drain_ticks` to advance a controlled 8 or 12 setup ticks before
observation zero; this drains the observer's vanilla on-add pulse rather than
silently accepting a phase-dependent starting lamp. The contaminated first
six-face run is preserved in
`trace/out/matrix_redstone_observer_six_faces_1/summary.md`, and the
deterministic rerun passes 6/6 in
`trace/out/matrix_redstone_observer_six_faces_2/summary.md`.
The behavior gates assert watched-face-only notification, +2 activation,
+2 release, priority/order, duplicate suppression, powered suppression,
directional weak/strong output, placement pulse, same-time observer-chain
ordering, saved pending activation, and powered pending removal through one
normal cube. The capsule admits only an exact bounded observer/lamp/inert
notification region and restores due/priority/order without sending load-time
notifications. Focused evidence passes 12/12 at
`trace/out/matrix_redstone_observer_family_1/summary.md`; the promoted
145-case aggregate is in
`trace/out/matrix_redstone_observer_full_1/summary.md`.

The piston fixtures place an unextended normal piston ID 33 in each of the six
facings with air in front, then add a redstone block on a non-output side at
tick zero. They deliberately check the earliest post-tick boundary: Java's
piston block event is not a scheduled tick and drains in the same server tick,
leaving each facing-specific extended base and moving head 36 at observation
zero. The EAST fixture additionally proves that block 36:5 remains through
observation one and settles to head 34:5 at observation two. Native controls
loop over all six facing/offset pairs and gate the internal EAST
`lastProgress/progress` sequence `0/0.5 -> 0.5/1 -> retire` plus an
obsidian-blocked no-move circuit.
The clean omission and first fix are in
`trace/out/redstone_piston_east_empty_extension_probe_1/summary.md` and
`trace/out/redstone_piston_east_empty_extension_fix_1/summary.md`. The
expanded focused family passes 9/9 at
`trace/out/matrix_redstone_piston_empty_extension_six_faces_family_1/summary.md`;
the 154-case aggregate is in
`trace/out/matrix_redstone_piston_empty_extension_six_faces_full_1/summary.md`.
The first added power-source fixture flips an adjacent floor lever from
69:5 to 69:13 at tick zero. The exact omission/fix evidence is in
`trace/out/redstone_piston_east_lever_empty_extension_probe_1/summary.md` and
`trace/out/redstone_piston_east_lever_empty_extension_fix_1/summary.md`; the
expanded piston family passes 10/10 at
`trace/out/matrix_redstone_piston_powered_lever_family_1/summary.md`, and the
promoted aggregate passes 155/155 at
`trace/out/matrix_redstone_piston_powered_lever_full_1/summary.md`.
Stone and wooden buttons have parallel setter-edge fixtures changing
metadata 5-to-13. Their clean omission/fix pairs are under
`trace/out/redstone_piston_east_button_empty_extension_{probe,fix}_1` and
`trace/out/redstone_piston_east_wooden_button_empty_extension_{probe,fix}_1`.
The direct-control family passes 12/12 at
`trace/out/matrix_redstone_piston_direct_controls_family_1/summary.md`, and
the promoted aggregate passes 157/157 at
`trace/out/matrix_redstone_piston_direct_controls_full_1/summary.md`.
These fixtures isolate immediate weak power; the existing independent button
suite owns interaction-created release timing.
Pressure-plate fixtures cover IDs 70, 72, 147, and 148 at source transitions
0-to-1, 0-to-1, 0-to-7, and 0-to-1. The deliberate 4/4 omission matrix is at
`trace/out/matrix_redstone_piston_pressure_plates_probe_1/summary.md`; the
corrected 4/4 result is at
`trace/out/matrix_redstone_piston_pressure_plates_fix_1/summary.md`.
The expanded direct-power family passes 16/16 at
`trace/out/matrix_redstone_piston_direct_power_sources_family_1/summary.md`,
and the promoted aggregate passes 161/161 at
`trace/out/matrix_redstone_piston_direct_power_sources_full_1/summary.md`.
Plate entity collision/rescheduling remains owned by the independent plate
fixtures rather than being implied by these setter-edge piston cases.
The next pair proves lit-torch face directionality. A floor torch on the
EAST piston's south side starts extension; a floor torch above it is queried
on its attachment face and remains an exact no-extension negative. The
deliberate probe at
`trace/out/matrix_redstone_piston_torch_direction_probe_1/summary.md`
isolates the positive base/head omission and an independent negative-case
light mismatch. Java carries block light 6 through the non-opaque piston base,
so piston IDs 29/33/34/36 now use opacity zero. The isolated light correction
passes at
`trace/out/redstone_piston_east_torch_attached_face_light_fix_1/summary.md`,
and the corrected directional pair passes 2/2 at
`trace/out/matrix_redstone_piston_torch_direction_fix_1/summary.md`.
The expanded family passes 18/18 at
`trace/out/matrix_redstone_piston_directional_torch_family_1/summary.md`,
and the promoted aggregate passes 163/163 at
`trace/out/matrix_redstone_piston_directional_torch_full_1/summary.md`.
The powered-repeater pair places stable redstone-block inputs behind two
tick-zero powered states. Repeater 94:0 outputs north into the piston;
repeater 94:1 outputs east and is the no-extension control. The deliberate
three-case omission matrix, which also retains the prior torch negative, is at
`trace/out/matrix_redstone_piston_repeater_direction_probe_1/summary.md`:
only the oriented repeater fails, at exactly the piston base/head pair. The
corrected three cases pass at
`trace/out/matrix_redstone_piston_repeater_direction_fix_1/summary.md`.
The expanded piston family passes 20/20 at
`trace/out/matrix_redstone_piston_directional_repeater_family_1/summary.md`,
and the promoted aggregate passes 165/165 at
`trace/out/matrix_redstone_piston_directional_repeater_full_1/summary.md`.
Comparator cases restore a powered comparator and exact tile output 15 before
placing the piston at tick zero. Comparator 150:0 outputs north; rotated
150:1 outputs east. The deliberate probe at
`trace/out/matrix_redstone_piston_comparator_direction_probe_1/summary.md`
isolates the positive piston base/head omission and shows that the rotated
case's only defect is powered-comparator light. Java registers block 150 with
emission 9; the corrected rotated proof matches all 10,625 cells at
`trace/out/redstone_piston_east_comparator_wrong_direction_light_fix_1/summary.md`.
The corrected direction matrix passes at
`trace/out/matrix_redstone_piston_comparator_direction_fix_1/summary.md`;
native controls additionally reject output zero. The expanded family passes
22/22 at
`trace/out/matrix_redstone_piston_directional_comparator_family_1/summary.md`,
and the promoted aggregate passes 167/167 at
`trace/out/matrix_redstone_piston_directional_comparator_full_1/summary.md`.
Observer piston inputs use a live pulse after eight fixture-drain ticks, not a
saved powered bit that vanilla clears in `onBlockAdded`. Tick zero places the
piston and tick one edits the watched cell. The SOUTH-watching 218:3 positive
pulses to 218:11 and outputs north; the rotated EAST-watching 218:5 negative
pulses to 218:13 without powering the piston. The valid deliberate omission at
`trace/out/matrix_redstone_piston_observer_direction_probe_2/summary.md`
isolates exactly the positive base/head pair, and the corrected three-case
matrix passes at
`trace/out/matrix_redstone_piston_observer_direction_fix_1/summary.md`.
The expanded family passes 24/24 at
`trace/out/matrix_redstone_piston_directional_observer_family_1/summary.md`,
and the promoted aggregate passes 169/169 at
`trace/out/matrix_redstone_piston_directional_observer_full_1/summary.md`.
The next four fixtures prove direct dust weak power and one adjacent
normal-cube relay. A north-south dust line beside the piston extends it, while
an equally powered east-west line is the direct negative. Dust on top of the
south-adjacent stone strongly powers that Java-normal cube and extends the
piston; powered east-west dust beyond the same stone is the indirect negative.
The deliberate matrix at
`trace/out/matrix_redstone_piston_wire_indirect_probe_1/summary.md` passes the
observer control and both negatives, while each positive differs at exactly
the piston base/head pair. All five corrected cases pass at
`trace/out/matrix_redstone_piston_wire_indirect_fix_1/summary.md`.
The expanded family passes 28/28 at
`trace/out/matrix_redstone_piston_directional_wire_indirect_family_1/summary.md`,
and the promoted aggregate passes 173/173 at
`trace/out/matrix_redstone_piston_directional_wire_indirect_full_1/summary.md`.
The next geometry probe copies the fixed Java `pos.up()` pass: a redstone
block one up and one side extends, a front/output-face block is excluded, and
the below-diagonal mirror does not power. The deliberate and corrected
matrices are
`trace/out/matrix_redstone_piston_quasi_connectivity_{probe,fix}_1/summary.md`;
the family passes 31/31 at
`trace/out/matrix_redstone_piston_quasi_connectivity_family_1/summary.md` and
the aggregate passes 176/176 at
`trace/out/matrix_redstone_piston_quasi_connectivity_full_1/summary.md`.
A single stone push then proves two moving tiles: both positions hold block
36 through start/progress, and tick three settles head 34:5 plus stone 1:0.
The deliberate and corrected matrices are
`trace/out/matrix_redstone_piston_single_stone_push_{probe,fix}_1/summary.md`;
the expanded family passes 34/34 at
`trace/out/matrix_redstone_piston_single_stone_push_family_1/summary.md`, and
the promoted aggregate passes 179/179 at
`trace/out/matrix_redstone_piston_single_stone_push_full_1/summary.md`.
Straight-line traversal then adds two-stone start/settlement and the exact
12-versus-13 boundary. The first long fixture crossed the player and the
second put the legal destination on an uncleared generated leaf; both are
retained as rejected fixture evidence, not parity claims. With the line moved
fully inside the verified cuboid, the corrected focused matrix passes 5/5 at
`trace/out/matrix_redstone_piston_stone_line_limit_fix_2/summary.md`, the
expanded family passes 38/38 at
`trace/out/matrix_redstone_piston_stone_line_limit_family_1/summary.md`, and
the aggregate passes 183/183 at
`trace/out/matrix_redstone_piston_stone_line_limit_full_1/summary.md`.
The implementation reads at most 13 forward cells, creates at most 13
fixed-pool entries, and leaves the idle path unchanged. This proof
is extended by the first exact reaction bundle. Birch planks 5:2 proves
non-stone NORMAL movement at start and settlement. Dandelion 37:0 is destroyed
both directly and after one moved stone, producing the exact EntityItem and
then receiving the exact moving-piston sweep. The runtime follows Java's
ordinary-entity-before-moving-tile order, six-facing head/full-cube swept
AABBs, overlap plus 0.01 displacement, and the per-axis 0.51 piston clamp.
The corrected reaction family passes 42/42 at
`trace/out/matrix_redstone_piston_block_reactions_family_1/summary.md`, and
the corresponding aggregate passes 187/187 at
`trace/out/matrix_block_reactions_full_rerun_1/summary.md`.
An allium 38:2 then supplies the first metadata-bearing flower drop. The
deliberate omission at
`trace/out/redstone_piston_front_allium_destroy_probe_1/summary.md` is
confined to the retracted base, intact flower, and missing item; the fix at
`trace/out/redstone_piston_front_allium_destroy_fix_1/summary.md` preserves
item metadata 2 and every RNG-derived entity field exactly. The expanded
piston family passes 43/43 at
`trace/out/matrix_redstone_piston_flower_destroy_family_1/summary.md`, and
the complete aggregate passes 188/188 at
`trace/out/matrix_redstone_piston_flower_destroy_full_1/summary.md`.
A supported floor torch adds the first drop whose item damage differs from
its block metadata. Java turns block 50:5 into item 50:0. The deliberate
omission and focused fix are at
`trace/out/redstone_piston_front_floor_torch_destroy_{probe,fix}_1/summary.md`.
The runtime now resolves each admitted DESTROY state through an explicit
payload mapper: flower variants retain metadata, while torch orientations
map to zero. The piston family passes 44/44 at
`trace/out/matrix_redstone_piston_torch_destroy_family_1/summary.md`, and the
full aggregate passes 189/189 at
`trace/out/matrix_redstone_piston_torch_destroy_full_1/summary.md`.
Supported redstone wire then proves a block-to-item-ID payload change:
`BlockRedstoneWire.getItemDropped` maps block 55 to item 331:0. The deliberate
omission/fix pair is at
`trace/out/redstone_piston_front_wire_destroy_{probe,fix}_1/summary.md`.
The piston family passes 45/45 at
`trace/out/matrix_redstone_piston_wire_destroy_family_1/summary.md`, and the
full aggregate passes 190/190 at
`trace/out/matrix_redstone_piston_wire_destroy_full_1/summary.md`.
Supported fire then proves a zero-drop DESTROY payload.
`BlockFire.quantityDropped` returns zero, so the runtime extends without
creating an EntityItem or consuming drop RNG/entity capacity, while retaining
the exact already-pending block-51 callback. The deliberate rejection and
focused fix are at
`trace/out/redstone_piston_stone_then_fire_destroy_{probe,fix}_1/summary.md`.
The piston family passes 46/46 at
`trace/out/matrix_redstone_piston_fire_no_drop_family_1/summary.md`, and the
full aggregate passes 191/191 at
`trace/out/matrix_redstone_piston_fire_no_drop_full_1/summary.md`.
Snow layer 78:3 then proves a filtered-drop path. Forge builds five snowball
candidate stacks but passes chance -1 for piston destruction, consuming five
World.rand chance draws while spawning no EntityItem. The deliberate probe
and focused fix are at
`trace/out/redstone_piston_snow_multidrop_probe_1/summary.md` and
`trace/out/redstone_piston_snow_suppressed_drop_fix_1/summary.md`.
The piston family passes 47/47 at
`trace/out/matrix_redstone_piston_snow_suppressed_drop_family_1/summary.md`,
and the full aggregate passes 192/192 at
`trace/out/matrix_redstone_piston_snow_suppressed_drop_full_1/summary.md`.
Brown and red mushrooms then prove their shared deterministic default-drop
class with independent registered item IDs 39:0 and 40:0. The deliberate
pair/fix is at
`trace/out/redstone_piston_mushroom_destroy_{probe,fix}_1/summary.md`.
The piston family passes 49/49 at
`trace/out/matrix_redstone_piston_mushroom_destroy_family_1/summary.md`, and
the aggregate passes 194/194 at
`trace/out/matrix_redstone_piston_mushroom_destroy_full_1/summary.md`.
An attached ladder 65:5 then proves orientation stripping to item 65:0 under
an indirectly queued piston event. The engine omission is at
`trace/out/redstone_piston_attached_ladder_destroy_probe_1/summary.md`.
The first payload fix at
`trace/out/redstone_piston_attached_ladder_destroy_fix_1/summary.md` made raw
blocks exact but exposed a Java harness error: tick-zero source placement
queued the neighboring piston event after cursor restore, while the bridge
immediately drained events only when the edited block itself was ID 29/33.
The event therefore ran after excluded work changed World.rand, Math.random,
and the next-entity-ID cursor. The bridge now drains the post-edit block-event
queue at the controlled input boundary for every edit; an empty queue is a
no-op. The exact focused result is
`trace/out/redstone_piston_attached_ladder_destroy_fix_2/summary.md`.
The piston family passes 50/50 at
`trace/out/matrix_redstone_piston_ladder_destroy_family_1/summary.md`, and the
aggregate passes 195/195 at
`trace/out/matrix_redstone_piston_ladder_destroy_full_1/summary.md`.
Support-independent cobweb 30:0 then proves another block-to-item rule:
`BlockWeb.getItemDropped` returns string item 287:0. The deliberate omission
and focused fix are at
`trace/out/redstone_piston_cobweb_destroy_{probe,fix}_1/summary.md`.
The piston family passes 51/51 at
`trace/out/matrix_redstone_piston_cobweb_destroy_family_1/summary.md`, and the
aggregate passes 196/196 at
`trace/out/matrix_redstone_piston_cobweb_destroy_full_1/summary.md`.
Ordinary/lit pumpkins 86:3/91:3 then prove their shared `BlockPumpkin`
orientation rule: both emit one same-ID item with damage zero, while moving
block 36 removes lit pumpkin's block-light field. The deliberate pair/fix is
at `trace/out/redstone_piston_pumpkin_destroy_{probe,fix}_1/summary.md`.
The piston family passes 53/53 at
`trace/out/matrix_redstone_piston_pumpkin_destroy_family_1/summary.md`, and
the aggregate passes 198/198 at
`trace/out/matrix_redstone_piston_pumpkin_destroy_full_1/summary.md`.
Structure void 217:0 then proves an empty drop-method override: it is
destroyed without consuming drop RNG, entity ID, or capacity. The deliberate
probe/fix is at
`trace/out/redstone_piston_structure_void_destroy_{probe,fix}_1/summary.md`.
The piston family passes 54/54 at
`trace/out/matrix_redstone_piston_structure_void_destroy_family_1/summary.md`,
and the aggregate passes 199/199 at
`trace/out/matrix_redstone_piston_structure_void_destroy_full_1/summary.md`.
Lever, stone/wood buttons, four pressure plates, lit/unlit redstone torches,
and powered/unpowered repeaters then add 11 exact block-specific drop
payloads over all 118 admitted canonical metadata states. Four indirect-lamp
fixtures lock their powered break-notification geometry. The powered-repeater
fixture also locks Java's ordering of dropped-item SELF collision against a
half-extended piston head before swept piston motion. The deliberate and
focused evidence is at
`trace/out/redstone_piston_control_destroy_probe_1/summary.md`,
`trace/out/redstone_piston_control_destroy_bundle_1/summary.md`, and
`trace/out/redstone_piston_control_break_notify_{probe_1,fix_2}/summary.md`.
The first exact-source rerun exposed a west-moving lever item crossing the
settled ID-34 piston-head plate at tick 3. The deliberate current-source
failure is preserved at
`trace/out/redstone_piston_control_destroy_bundle_current_1/summary.md`.
The active-item path now reuses the captured two-box
`BlockPistonExtension` plate/non-SHORT-arm geometry for every static-head
facing and searches the one-cell overhang. The focused correction passes at
`trace/out/redstone_piston_control_settled_head_fix_1/summary.md`, and all 15
slice cases pass on the final source at
`trace/out/redstone_piston_control_destroy_bundle_current_fix_1/summary.md`.
The current-source piston family passes 69/69 at
`trace/out/matrix_redstone_piston_control_settled_head_family_1/summary.md`,
and the aggregate candidate passes 214/214 at
`trace/out/matrix_redstone_piston_control_settled_head_full_1/summary.md`: all
214 state/raw-block gates and 211 required behavior gates pass, with three
explicitly not-required rows.
Dead bush 32:0 then adds one `nextInt(3)` count and 0–2 separate stick 280:0
stacks. The count-two deliberate/focused pair is at
`trace/out/redstone_piston_dead_bush_destroy_{probe,fix}_1/summary.md`.
Native tests lock both per-stack RNG/ID sequences, count zero, and atomic
insufficient-capacity rejection. The piston family passes 70/70 at
`trace/out/matrix_redstone_piston_dead_bush_family_1/summary.md`, and the
exact-current-source aggregate passes 215/215 at
`trace/out/matrix_redstone_piston_dead_bush_full_1/summary.md`: all 215
state/raw-block gates and 212 required behavior gates pass, with three
explicitly not-required rows.
Sapling 6 then adds all 12 canonical wood-type/stage states. Item metadata
retains type 0..5 and strips stage bit 8. The two endpoint probe/fix is at
`trace/out/redstone_piston_sapling_destroy_{probe,fix}_1/summary.md`, the
piston family passes 72/72 at
`trace/out/matrix_redstone_piston_sapling_family_1/summary.md`, and the
exact-current-source aggregate passes 217/217 at
`trace/out/matrix_redstone_piston_sapling_full_1/summary.md`: all 217
state/raw-block gates and 214 required behavior gates pass, with three
explicitly not-required rows.
A successful extension also consumes one World.rand `nextFloat()` for sound
pitch after `doMove`, even when audio output is disabled. The paired fixture
`redstone_piston_dual_oak_sapling_destroy` places one redstone block between
opposed pistons, relying on WEST-then-EAST neighbor order so the second exact
item exposes that intervening draw inside one uncontaminated server boundary.
The deliberate omission fails entity state at tick zero but keeps raw blocks
exact at `trace/out/redstone_piston_dual_sound_rng_probe_1/summary.md`; the
focused correction passes at
`trace/out/redstone_piston_dual_sound_rng_fix_1/summary.md`. The piston family
passes 73/73 at
`trace/out/matrix_redstone_piston_dual_sound_rng_family_1/summary.md`; the
full matrix passes 218/218 at
`trace/out/matrix_redstone_piston_dual_sound_rng_full_1/summary.md`: all 218
state/raw-block gates and 215 required behavior gates pass, with three
explicitly not-required rows.
Tall grass 31:0..2 then adds process-global `Block.RANDOM` to the exact replay
boundary. Unlike `World.rand`, that cursor is not stored in vanilla world NBT,
so the bridge captures its private internal 48-bit state and restores it just
before controlled block edits. Internal seed 0 takes the successful
`nextInt(8)`, `nextInt(10)`, `nextInt(1)` path and creates wheat seeds 295:0;
seed 1396 takes the one-draw no-drop path. The deliberate omission of only
`nextInt(1)` leaves blocks/items unchanged but fails the specialized cursor
gate at `trace/out/redstone_piston_tall_grass_block_rng_probe_1/summary.md`.
Both branches pass at
`trace/out/redstone_piston_tall_grass_fix_1/summary.md`. The piston family
passes 75/75 at
`trace/out/matrix_redstone_piston_tall_grass_family_1/summary.md`, and the
full matrix passes 220/220 at
`trace/out/matrix_redstone_piston_tall_grass_full_1/summary.md`: all 220
state/raw-block gates and 217 required behavior gates pass, with three
explicitly not-required rows.

Wheat 59:0..7 adds exact age-dependent `BlockCrops.getDrops`: immature ages
emit one seed, while age 7 emits wheat then consumes three
`World.rand.nextInt(14)` seed trials. The seed-zero mature fixture produces
wheat plus two separate seed stacks; the immature fixture consumes no count
trials. Both moving heads also notify the supporting farmland to become dirt
because moving-piston material is solid. `capture_blockstate_props.py` now
generates canonical-metadata and `Material.isSolid` masks from the live Java
registry; the capsule pins capture digest
`65ffa359e5bec3874dce5225f23b9f36e1f54df93150037faa1ba325167d1ebd`.
The pre-fix negative is
`trace/out/redstone_piston_mature_wheat_probe_3/summary.md`; both focused fixes
pass at `trace/out/redstone_piston_wheat_fix_3/summary.md`. The deliberate
transition-count negative is exercised by `game/test_script.sh`. The targeted
13-case harness rerun passes at
`trace/out/controlled_transition_fix_1/summary.md`, and the complete matrix
passes 222/222 at
`trace/out/matrix_redstone_piston_wheat_full_2/summary.md`: all 222 state and
raw-block gates pass, with 219 required behavior rows and three not-required
rows.

The latest clean frozen performance guard is
`trace/out/perf_guard_redstone_piston_wheat_1.json`: 4,241 scalar steps/s,
2.92M Blaze env-ticks/s, and 26.71 1080p CUDA fps, all above the
unchanged floors. The final build uses current sources and a native sm_120
CUDA raster object. Earlier control-candidate failures remain retained as
host-contention evidence; no floor or baseline changed.
Other DESTROY algorithms, non-full moving shapes, non-item entity collision,
retraction/sticky behavior, slime structure branching, moving-tile capsule
restore, and rendering remain intentionally unclaimed.

Tripwire hooks 131 and wires 132 extend that boundary through attached-line
teardown and represented entity activation. The stationary-item fixture proves
the contacted wire, both hooks, both lamps, pending hook callback, occupied
wire callback, and full raw/light cuboid against Java for 12 ticks. The
focused result is
`trace/out/redstone_tripwire_item_occupied_fix_2/summary.md`. The moving-player
fixture additionally proves non-solid crossing, activation, complete exit, and
the exact +10 release callback. All seven tripwire cases pass at
`trace/out/redstone_tripwire_family_release_fix_1/summary.md`, and the complete
matrix passes 288/288 at
`trace/out/matrix_redstone_tripwire_release_full_2/summary.md`. Pending tripwire hook
work is admitted only when `tripwire_hook_proof_region` validates the complete
attached line and bounded notification neighborhood. Additional non-item Java
trigger coverage remains unclaimed.

Carrot 141 and potato 142 piston destruction use the same proof boundary as
wheat but retain their distinct items and RNG. The mature path consumes three
`World.rand.nextInt(14)` trials before item spawning; mature potato then uses
the independent `Block.RANDOM.nextInt(50)` poisonous-potato trial. Focused
carrot/potato coverage passes 3/3 at
`trace/out/redstone_piston_carrot_potato_fix_1/summary.md`, the shared crop
family passes 5/5 at `trace/out/redstone_piston_crop_family_fix_1/summary.md`,
and the complete matrix passes 291/291 at
`trace/out/matrix_redstone_piston_carrot_potato_full_1/summary.md`. Native
coverage exhausts all 16 canonical ages, both poison branches, invalid
metadata, exact cursor order, and atomic insufficient-capacity rejection.

Comparator 149/150 piston destruction emits item 404:0, removes the comparator
tile, and preserves Java's ordered break notifications. The powered-output
fixture proves the lamp's +4 release; the direct saved block-150 fixture proves
the stale +2 self-correction callback created before destruction. The focused
three-case result passes at
`trace/out/redstone_piston_comparator_destroy_fix_4/summary.md`, the comparator
piston family passes 5/5 at
`trace/out/redstone_piston_comparator_family_fix_1/summary.md`, and the clean
aggregate passes 294/294 at
`trace/out/matrix_redstone_piston_comparator_destroy_full_2/summary.md`.
Native coverage exhausts both IDs and all metadata states, powered output
teardown, tile retirement, exact pending work, and fixed-pool rejection.

Beetroot 207 piston destruction completes the generated registry-order crop
gap. Ages 0..2 emit one seed 435:0; mature age 3 emits beetroot 434:0 and then
uses three `World.rand.nextInt(6)` trials for seed bonuses. The deliberate
old-C result fails 2/2 at
`trace/out/redstone_piston_beetroot_probe_1/summary.md`, the focused correction
passes 2/2 at `trace/out/redstone_piston_beetroot_fix_1/summary.md`, and the
seven-case crop family passes at
`trace/out/redstone_piston_crop_family_beetroot_fix_1/summary.md`. The full
matrix passes 296/296 at
`trace/out/matrix_redstone_piston_beetroot_full_1/summary.md`; one unrelated
pumpkin-stem oracle timeout passed after its isolated client was recycled.
Native coverage exhausts all four ages, invalid metadata, exact item and RNG
order, farmland conversion, and atomic fixed-pool rejection.

Settled normal-piston retraction now has distinct start, progress, and settled
oracle cases. Tick-zero source removal turns extended base 33:13 into moving
block 36:5, erases head 34:5, consumes the exact contraction-pitch RNG draw,
and settles base 33:5 on the third tile tick. The old-C result fails at
`trace/out/redstone_piston_empty_retraction_probe_1/summary.md`; all three
fixed cases pass at
`trace/out/redstone_piston_empty_retraction_fix_1/summary.md`, and the combined
extension/retraction family passes 6/6 at
`trace/out/redstone_piston_extension_retraction_family_fix_1/summary.md`.
The 299-case aggregate passes 298 rows; its sole known random-walk landing
capture failure passes 3/3 on fresh isolated clients at
`trace/out/random_seed_1_retraction_isolated_remeasure_1/summary.md`.

Sticky-piston one-stone extension and pulling have six distinct
start/progress/settled oracle cases. Extension uses typed sticky-head metadata
13 while the moving stone keeps facing metadata 5. Retraction creates source
and pulled-stone moving tiles in Java order and settles base 29:5 with the
stone in the former head cell. The deliberate old-C pull case fails at
`trace/out/redstone_sticky_piston_one_stone_pull_probe_1/summary.md`; the
focused correction passes 6/6 at
`trace/out/redstone_sticky_piston_one_stone_fix_1/summary.md`, the combined
normal/sticky family passes 12/12 at
`trace/out/redstone_piston_normal_sticky_lifecycle_family_1/summary.md`, and
the clean aggregate passes 305/305 in 268.544 seconds at
`trace/out/matrix_redstone_sticky_piston_one_stone_full_1/summary.md`.

Normal and sticky one-stone minimum pulses use a two-edit sequence: add a
side source at tick 0, remove it at tick 1 while extension tiles are active,
then observe through settlement. Java force-settles the moving head, lets the
normal moved stone continue, suppresses the sticky pull when that stone was
itself still extending, and starts source retraction. The deliberate old-C
proof fails at
`trace/out/redstone_sticky_piston_minimum_pulse_probe_1/summary.md`; the pair
passes 2/2 at
`trace/out/redstone_piston_minimum_pulse_normal_sticky_fix_1/summary.md`, and
the lifecycle/observer family passes 15/15 at
`trace/out/redstone_piston_minimum_pulse_lifecycle_family_1/summary.md`.
Two complementary full aggregates plus fresh 3x isolated remeasures cover all
307 current cases; see `magma/PARITY_PROJECT.md` for the capture-instability
accounting.

Slime-structure branching adds four required oracle cases for an EAST piston
with slime and one attached UP stone. Normal extension and sticky pull each
check the first moving frame and the settled state, including exact six-cell
mutation sets, reverse structure order, sound RNG, entity cursor, scheduled
queue, and raw blocks. The affected lifecycle family passes 18/18 at
`trace/out/redstone_piston_slime_structure_family_1/summary.md`. The current
full matrix passes 311/311 in 285.943 seconds at
`trace/out/matrix_redstone_piston_slime_structure_full_1/summary.md`; the one
water-case infrastructure timeout passed on the built-in fresh-client retry.
Native controls cover the shared 12-block structure limit, base-only sticky
retraction on an oversized attachment, and side-obsidian exclusion.

Four more required oracle cases attach a terminal cobweb beyond that side
stone for normal extension and sticky pull, checking both the first moving
frame and settlement. They prove six exact mutations, reverse destroy-before-
move ordering, string item 287, World/Math RNG, entity-ID allocation, and an
empty scheduled queue. The promoted set passes 4/4 at
`trace/out/redstone_piston_slime_cobweb_destroy_promoted_1/summary.md`, and the
expanded affected family passes 23/23 at
`trace/out/redstone_piston_slime_destroy_family_1/summary.md`. The full matrix
passes 313/315 in 264.611 seconds at
`trace/out/matrix_redstone_piston_slime_destroy_full_1/summary.md`; both
unrelated capture misses pass 3/3 in the isolated remeasure, so the composite
covers all 315 cases. Native controls also check two destroy branches and
atomic rejection when item capacity is insufficient. The bounded runtime
at that promotion boundary admitted only simple terminal payloads; paired,
cascading, stateful, and randomized terminal destroy behavior remained open.

Canonical bed pairs add eight required cases: foot and head targets for both
normal extension and sticky pull, each at the first moving frame and after
settlement. A foot supplies the direct bed item and its head disappears in the
ordered notification pass; a head supplies no direct item and its notified
foot owns the deferred drop. The old-C probe has four expected foot-target
failures while the four capacity-available head cases already pass. The fixed
set passes 8/8 at
`trace/out/redstone_piston_slime_bed_pair_fix_1/summary.md`, the affected
family including ordinary-bed controls passes 33/33 at
`trace/out/redstone_piston_slime_bed_pair_family_1/summary.md`, and the clean
full aggregate passes 323/323 in 294.370 seconds at
`trace/out/matrix_redstone_piston_slime_bed_pair_full_1/summary.md`. Native
coverage reserves the deferred foot drop before mutation and rejects a full
item pool atomically. Other paired/cascading payloads remain separate work.

Canonical door pairs add another eight required cases: lower and upper targets
for normal extension and sticky pull, at the first moving frame and after
settlement. A lower half supplies its direct item; an upper half supplies no
direct item and its notified lower mate owns the deferred item. The deliberate
old-C probe fails the four lower-target cases while the four capacity-available
upper-target cases pass. The corrected set passes 8/8 at
`trace/out/redstone_piston_slime_door_pair_fix_1/summary.md`, the affected
family including eight ordinary-door controls passes 49/49 at
`trace/out/redstone_piston_slime_door_pair_family_1/summary.md`, and the clean
full aggregate passes 331/331 in 295.020 seconds at
`trace/out/matrix_redstone_piston_slime_door_pair_full_1/summary.md`. Native
coverage checks all seven door IDs and item mappings across all 84 canonical
lower/upper states. A full item pool rejects the deferred upper-target path
atomically. Other paired/cascading payloads remain separate work.

Deterministic double plants add eight required double-rose cases: lower and
upper targets for normal extension and sticky pull, at the first moving frame
and after settlement. The old-C probe has four lower-target failures and four
capacity-unaware upper-target passes. The corrected set passes 8/8 at
`trace/out/redstone_piston_slime_double_rose_pair_fix_1/summary.md`, and the
affected family including all five ordinary double-plant controls passes 62/62
at `trace/out/redstone_piston_slime_double_rose_pair_family_1/summary.md`.
The full matrix passes 338/339 in 285.933 seconds at
`trace/out/matrix_redstone_piston_slime_double_rose_pair_full_1/summary.md`;
its unrelated random-walk miss passes 3/3 at
`trace/out/slime_double_rose_unrelated_random_remeasure_1/summary.md`, so the
composite covers all 339 cases. Native coverage checks all 40 deterministic
canonical pairs plus fixed-pool atomic rejection. At that promotion boundary,
randomized double grass remained an explicit multi-entry preflight boundary.

Randomized double grass adds ten required cases. Eight cover lower/upper
targets on extension and sticky pull; two cover a mixed structure with one
direct lower roll and one deferred upper-to-lower roll in distinct Java
phases. The deliberate old-C probe fails 10/10, while the corrected set passes
10/10 at `trace/out/redstone_piston_slime_double_grass_pair_fix_1/summary.md`.
The affected family passes 72/72 at
`trace/out/redstone_piston_slime_double_grass_pair_family_1/summary.md`. The
full matrix passes 348/349 in 301.386 seconds at
`trace/out/matrix_redstone_piston_slime_double_grass_pair_full_1/summary.md`;
its unrelated random-walk miss passes 3/3 at
`trace/out/slime_double_grass_unrelated_random_remeasure_1/summary.md`, so the
composite covers all 349 cases. Native tests prove exact drop/no-drop capacity,
RNG, entity-ID, and atomicity behavior, including two selected drops with one
free slot. Mixed deferred grass plus tripwire-break sound RNG remains a
separate callback topology.

## State capsules

`state_capsule.py` defines a versioned, checksummed neutral manifest plus a
packed raw-block payload and an optional saved-skylight payload over the same
inclusive cuboid/order. Version 2 restores exact player
pose/motion/vitals/air/fire, FoodStats internals, XP and its hidden total,
combat timers, absorption, up to 32 ordered potion effects, the client
movement-packet cursor, all 41 inventory/armor/offhand slots with the promoted
enchantment subset, dimension, the complete isolated weather/daylight clock,
an inclusive block cuboid, living
NoAI pigs, XP orbs, inert scheduled stone, and proof-safe horizontal/downward
water updates in a bounded two-air-layer basin over a flat stone floor.
It also restores a deterministic level-0 lava source on the flat plane, the
enclosed downward lava/water reaction pair, and metadata-0 sand over a clear
air column ending at stone. Proof-safe redstone subsets include lamp, button,
torch, repeater, comparator, stone-pressure-plate, and
weighted-pressure-plate callbacks in bounded validated neighborhoods.
Wooden-button promotion is restricted to an imminent arrow-free release and
rejects any captured vanilla arrow subtype. Repeater promotion requires
registry-backed support, represented input/output states, air above, and
repeater-only side inputs. Comparator promotion additionally requires a
complete captured comparator-tile list, exact output signals in 0..15, and a
bounded represented rear/side/output neighborhood. Direct cake, cauldron, and
End-frame overrides are admitted only with valid metadata. A normal-cube rear
input is admitted when it is an exact furnace override, or when its represented
second block is a supported static/container override. Chest promotion
requires a complete bounded container list, exactly 27 slots, valid
item/count/metadata tuples, and a schema/block-ID match. An ordinary single
chest requires represented non-chest blocks in all four horizontal neighbor
cells; a closed single trapped chest likewise requires no represented or
hidden adjacent block ID 146, so an unmodeled pair cannot be admitted.
Double-chest promotion instead requires exactly two horizontally adjacent
27-slot entries whose pair coordinates point back to each other, matching
ordinary or trapped schemas/block IDs, with no third same-type chest. Every
admitted chest requires represented air above;
blocked-container and unrepresented sitting-ocelot ambiguity are rejected by
the Java capture/capsule boundary. Trapped-chest capture additionally rejects
nonzero viewers, `lidAngle`, or `prevLidAngle`.
Furnace promotion requires exactly three slots, all four progress fields, and
block ID 61 or 62 within the represented cuboid. Dispenser/dropper promotion
requires exactly nine slots, the matching block ID 23/158, no deferred loot
table, and no more than 256 represented static-container tiles. Container
slots are restored before comparator output signals and scheduled callbacks.
Jukebox promotion shares that 256-tile bound but requires size 1, block ID 84,
record/metadata agreement, and the exact untagged vanilla-record subset above.
Command-block promotion has its own inventory-free schema, matching block ID,
success-count range, 256-tile bound, and no-same-cell scheduled-command proof.
The Java bridge additionally rejects every non-default execution-bearing NBT
field before marking the nearby tile list complete.
Player skulls carry their complete `NBTUtil.writeGameProfile` compound as an
uncompressed-NBT sidecar. Every sidecar is bounded to 1 MiB, the capsule total
is bounded to 16 MiB, length and SHA-256 are verified, and the root/profile
schema is validated before an event is emitted. C keeps the bytes only in its
allocate-on-use skull pool and wraps them under `SkullOwner` when the block's
drop hook creates ItemSkull 397:3. Trace comparison decodes all twelve vanilla
NBT tag types and compares compound fields semantically rather than relying on
Java `HashMap` insertion order.
Closed shulker boxes use the same bounded NBT route. Their sidecar is the
complete dropped ItemStack tag, with a required `BlockEntityTag`; the capsule
validates typed 27-slot alignment, nested tags, optional custom name/lock, and
the mutually exclusive deferred `LootTable`/nonzero `LootTableSeed` branch.
Custom names must also match the outer `display.Name`. C retains this payload
in allocate-on-use cold state and transfers it byte-for-byte to the one dropped
colored shulker item without filling deferred loot.
Item-frame comparator promotion has a separate complete entity list and
requires the exact bounded empty/plain-stone subset, unique IDs, finite pose,
hanging-air and normal-cube-support proof, one qualifying facing match, and a
represented comparator callback. Unsupported item/tag/drop/map state and
ambiguous multiple-frame matches are rejected.
Open/lid-transient trapped-chest capsule state and other container types remain
rejected; the live input fixture above covers viewer power without claiming
that saved-state boundary.
Pending updates retain absolute due time, priority, and relative tie-break
order inside the capsule cuboid.
The private 48-bit `World.rand`, process-global `Math.random`, and
process-global `Block.RANDOM` cursors plus signed 32-bit `World.updateLCG`
cursor are captured and restored exactly. `Block.RANDOM` is replay-only
process state beyond vanilla world NBT, not a claim about ordinary save-file
persistence. Loaded-chunk
ordering and complete random-tick section membership are fixture sidecar
evidence, not yet general capsule state, so this remains a bounded capsule.
When `sky_light.u8` is present, every value is checked as a 0..15 nibble,
length and SHA-256 are validated, block-derived lighting is resolved once,
and the saved values become the exact pre-tick boundary. Without that payload,
the capability ledger reports skylight as captured-only.
The exact pig payload
includes identity, pose, motion, health, and combat timers. The exact orb
payload also includes value, age, pickup delay, color, and its hidden
target-refresh cursor. Events are normalized into a deterministic order before
either engine applies them.
Capture and load are cold hooks: they run before the exact-step gate, not
inside the simulation hot loop.

The permanent mixed regression proves one nonvacuous 20-tick continuation with
player XP/combat/potions, enchanted armor and offhand equipment, an unopened
NoAI villager, a full chest, comparator callback, independent RNG cursors, and
all 10,625 raw block, block-light, and saved-skylight cells equal to Java. The
causal XP regression starts from a nonzero saved bar and proves that collecting
an orb updates level, fraction, and hidden total identically.

```bash
cd magma
uv run --no-project python trace/state_capsule.py selftest
bash trace/run_mixed_capsule_regression.sh
bash trace/run_player_xp_pickup_regression.sh
```

The self-test covers round-trip identity, deterministic entity/scheduled
ordering, duplicate IDs/updates, missing exact-orb-field rejection,
incomplete-state rejection, and checksum rejection. Arbitrary ItemStack NBT,
general entity/task payloads, general scheduled/tile work, per-entity RNGs,
and the complete loaded
random-tick active set remain O-02 backlog; the capsule must not be described
as a complete arbitrary-world save yet.
The integrated client's process-global next-entity-ID cursor is captured-only:
client entity construction outside a bounded capsule consumes it. Emergent
falling blocks compare by origin and block state while preserving raw EIDs for
diagnostics.

It can start missing local clients itself:

```bash
uv run --no-project python trace/run_oracle_matrix.py \
    --instances 4 --start-pool --seeds 0,1,2,3
```

The normal full-host lane is one command:

```bash
bash trace/run_oracle_matrix_pool.sh --out trace/out/matrix
```

Missing clients launch concurrently and then pass one readiness barrier. The
case executor already assigns one case per ready client, so increasing
`--instances` scales the slow Java reset/capture path without changing case
semantics. The fixed 64-case benchmark measured 21.23, 35.38, and 53.68
cases/minute at 8, 16, and 32 clients. The complete 248-case matrix took
209.437 seconds at 32 clients versus 1,232.674 seconds for the preceding
four-client run, about 5.9x lower wall time. Use the wrapper for 32 clients so
memory remains bounded and exact instances are always retired.
Progress output includes a completed/total counter so long promotion runs can
be followed without counting case lines.
Use `--case NAME --repeat N` to dispatch independent copies of one suspect
case across the pool. This is the fast lane for proving that a scheduled-RNG
or lifecycle fix is stable before paying for the complete aggregate.

Measure the same fixed 64-case workload at 8, 16, and 32 clients, including
cases/minute, host CPU, oracle CPU cores, peak host memory, pool RSS, and case
latency:

```bash
bash trace/run_oracle_pool_benchmark.sh
```

The wrapper places the complete process tree, including detached Java clients,
under a cgroup-v2 300 GiB soft throttle and 350 GiB hard limit, then stops the
exact pool instances in parallel on exit. The Python benchmark also refuses to
start another level above 350 GiB host usage and warns above 250 GiB. Its fixed workload is in
`trace/oracle_pool_benchmark_cases.txt`; keep that list unchanged between runs
being compared.
Use `--resume` with the same `--out` directory to append a level after an
infrastructure failure without rerunning completed levels.

Clients remain available for later runs. Stop exact instances without affecting
the primary VNC client:

```bash
java/start_oracle_instance.sh stop 0
java/start_oracle_instance.sh stop 1
```

For a single case against a non-default bridge, set `QRL_HOST`, `QRL_PORT`, and
`OUT` when invoking `run_oracle.sh`. Set `SKIP_BUILD=1` when a caller has already
built `trace_game`.

## Confounds to keep honest (the oracle EXPOSES these; it does not hide them)

- **Two different worldgens.** The magma world and the Java world are NOT the same terrain
  even at seed 0: magma's origin column (8,8) has surface y=80, while the real Java world at
  that column is near-void (ground ~y=3). So a bare pose-tp drops the Java player into a
  ~76-block free-fall that ends in death at ~tick 25, cascading into look/vitals/death. The
  `--platform` pad neutralizes this so the per-tick diff measures the PHYSICS MODEL, not the
  terrain gap. The pad is staged numerically before teleport, clears three air layers,
  and is read back from the authoritative server world; walking off it still
  re-introduces raw terrain differences.
- **`deaths` resets on a fresh oracle world.** A non-fresh evolved-world test may
  intentionally retain session state and must declare its baseline.
- **Inventory/held id namespaces differ** (Java vanilla registry vs blaze `IC_*`).
  The normal harness clears both inventories to avoid an artificial tick-0 mismatch;
  tests that deliberately stage items must translate and compare the two namespaces.
- **`sneaking`/`jumping` are tape intent in the small tracer.** Sprint engagement
  is simulated. Fixtures that test hunger/collision suppression must compare the
  full-runtime state after O-01 in `PARITY_PROJECT.md`.

## One-command usage

```bash
cd magma

# 1. build the C tracer (no Makefile edits; mirrors the magma_game link line)
bash trace/build_c_tracer.sh

# 2. generate a 300-tick tape
uv run --no-project python trace/gen_tape.py --ticks 300 --seed 0 --out trace/out/tape.txt

# 3. C side (headless, CPU; renders a small 320x180 frame for the hash)
./trace_game --tape trace/out/tape.txt --seed 0 --out trace/out/c_phys.csv

# 4. Java side - needs the live game (see below), then:
uv run --no-project python trace/trace_java.py \
    --tape trace/out/tape.txt --out trace/out/java_phys.csv --seed 0

# 5. diff, and materialize frames around the first divergence
uv run --no-project python trace/diff_trace.py \
    --java trace/out/java_phys.csv --c trace/out/c_phys.csv \
    --materialize --tape trace/out/tape.txt --seed 0
```

Tool self-check (proves the diff is correct): a log diffed against a copy of itself must
report **ZERO** divergence.

```bash
cp trace/out/c_phys.csv /tmp/c_copy.csv
uv run --no-project python trace/diff_trace.py --java trace/out/c_phys.csv --c /tmp/c_copy.csv
```

## Launching the Java game (anvil, headless on :1)

The Java tracer needs the client running with the qrl bridge on `127.0.0.1:25575`
(root `AGENTS.md` / `docs/RUNBOOK.md`, Run B/C):

```bash
cd java && setsid nohup bash start_vnc_client.sh >/tmp/mc_launch.out 2>&1 &
# wait until a TCP connect to 127.0.0.1:25575 succeeds (~20s), then run trace_java.py.
# on a fresh checkout you may need MC_GRADLE_ONLINE=1 (see AGENTS.md / docs/RUNBOOK.md).
```

`trace_java.py` calls `reset({"seed":0})`, which auto-loads the world and polls until
ready. Software GL (llvmpipe) is used on purpose so this stays off the shared GPU.

## Disk efficiency

A clean run costs only the two small CSVs. Frames are stored **only** when a divergence
is found and `--materialize` is passed: `diff_trace.py` re-runs the C tracer with
`--dump-dir out/diverge_<tick> --dump-lo T-K --dump-hi T+K` to write just that window as
PPMs (`c_<tick>.ppm`). K is `--window` (default 2).

### Grabbing the matching REAL-MC frame at a divergence tick

The Java side does not store frames per tick. To capture the real MC frame at tick `T`,
reuse the ffmpeg x11grab approach from `../verify/mc_capture/capture.sh`: with the
live game on display `:1`, replay the tape up to tick `T` (a trimmed tape via
`trace_java.py`), locate the MC window with `xwininfo -root -tree | grep 'Minecraft'`,
then `ffmpeg -f x11grab -video_size WxH -i :1.0+AX,AY -frames:v 1 out/diverge_T/mc_T.png`.
Place it next to the C `c_<tick>.ppm` for a side-by-side pixel comparison.

## Pose-forced FIRST-MINUTE FRAME ORACLE (`frame_oracle.py`)

The tick-trace above finds physics divergence. The **frame oracle** finds *rendering*
divergence over the first ~minute of scripted play - terrain lighting, missing assets,
rotated UVs, the hand, the HUD - as a named **tick + screen region + magnitude**.

The hard part is that the two games do **not** stay at the same player pose (physics
differs; there is already a tick-0 spawn divergence), so a naive frame-vs-frame diff is
meaningless. The oracle is **pose-forced**: at CHECKPOINT ticks (every `--cadence`, e.g.
every 60 ticks = ~20 checkpoints over a 1200-tick / 60s tape) it takes magma's exact
player pose from `c_phys.csv`, **teleports the Java player there**, grabs the Java frame,
renders the **magma** frame at the SAME pose, and pixel-diffs. Both cameras are then
identical, so a diff measures what the two RENDERERS draw differently - full stop.

Pose maths (per checkpoint row of `c_phys.csv`): the CSV stores world FEET coords and
MC-convention `yaw/pitch`. Java `tp` sets FEET directly (`tp @a x y z mc_yaw mc_pitch`);
magma renders with the EYE (`eye_y = feet_y + 1.62`, `PSV_EYE_HEIGHT`) and its own
convention `magma_yaw = 180 - mc_yaw`, `magma_pitch = -mc_pitch`. MC eye and magma
eye then coincide.

Three regions are diffed per checkpoint (via `render-opt/wholeframe/diff_frame.py`):
**whole** frame, a **terrain crop** (central band, excludes top sky + bottom HUD - isolates
lighting/geometry), and a **HUD region** (bottom strip: hotbar / vitals / hand base). The
per-checkpoint table localizes each divergence (`TERRAIN`, `HUD/hand`, or `near-floor`).

The target is the fill-rule noise floor (~0.02% of pixels; our rasterizer != GL), **not**
literally 0. Today the numbers are large by design - the lighting model differs and magma
draws no hand/HUD - and the oracle's job is to make that concrete and localized so we can
drive it down.

### Files

- `frame_oracle.py` - the orchestrator (one command). Runs `trace_game` -> `c_phys.csv`,
  picks checkpoints, renders each magma checkpoint frame with `game_candidate` (the
  arbitrary-pose renderer under `../../verify/mc_capture/`), optionally drives the live
  Java game to grab the matching frames, diffs all three regions, and prints the table +
  aggregate + ranked worst offenders. Disk-efficient: only checkpoints whose whole %-diff
  exceeds `--noise-pct` get a materialized MC / magma / heat-map trio (under
  `out/frame_oracle/worst/`).
- `../../verify/mc_capture/capture_at_poses.sh` - the Java-side engine: reads a poses
  FILE (one `IDX FEET_X FEET_Y FEET_Z MC_YAW MC_PITCH` per line), launches the primary
  headless game unless `--no-launch`, resets + freezes clear noon, teleports to each pose,
  and x11grabs the selected MC window -> `mc_ck_<idx>.png`. `--host`, `--port`, and
  `--display` select a pooled client; its temporary handshakes are port-scoped, so captures
  on multiple instances do not collide. Default gamemode is `survival` (draws hand +
  hotbar + crosshair + vitals HUD, so the oracle SEES the HUD/hand divergence magma
  lacks); `--gamemode spectator` gives a clean camera hold instead.

### One-command usage

```bash
cd magma

# magma-side only (no live game): renders magma checkpoints and validates the
# render+diff wiring against the existing aerial golden (mc_frame.png) as checkpoint 0.
uv run --no-project --with numpy --with pillow python trace/frame_oracle.py \
    --ticks 300 --cadence 60

# full pose-forced oracle including the live Java game on :1 (launches it itself;
# add --no-launch if a qrl bridge is already up):
uv run --no-project --with numpy --with pillow python trace/frame_oracle.py \
    --ticks 1200 --cadence 60 --run-java

# Same test against persistent pool instance 1, without disturbing the primary client.
uv run --no-project --with numpy --with pillow python trace/frame_oracle.py \
    --ticks 1200 --cadence 60 --run-java --no-launch --fresh-java \
    --java-port 25601 --java-display :21
```

On a fresh checkout the game launch needs `MC_GRADLE_ONLINE=1` once (see `AGENTS.md`).
Software GL (llvmpipe) keeps this off the shared GPU. The MC window is 854x480 / FOV 70,
matched by the magma render. Every live run also writes `frame_summary.json` with the
per-region metrics and exact Java endpoint. Add `--reuse-java --reuse-csv` to recompute
the report from existing checkpoint captures without driving either simulation again.

## Notes / known divergence sources (this is the point of the tool)

- **Spawn pose**: the C tracer spawns the player at its own worldgen origin column
  (`~8.5, surface+1, 8.5`, MC yaw 180); the Java game spawns at the REAL MC world spawn
  (a different x/y/z/pitch, on different terrain). So a raw java-vs-c diff diverges at
  tick 0 in x/y/z/pitch. To compare PHYSICS rather than spawn logistics, first teleport
  the Java player to the C spawn pose (a `tp` runcmd before the tape) or vice versa.
- **yaw** is compared as an angular difference (wraps at 360), so MC yaw 180 vs -180
  reads as zero divergence.
- **air / health**: both are strict fields. The drowning fixture matches the
  300-to--19 countdown, 2.5-health damage cadence, hurt timer, and dry reset.
  Hunger and the remaining survival effects still need their own non-vacuous
  fixtures before broad parity claims.
- Tolerances are flags: `--atol` / `--rtol` (floats), ints and `on_ground` are exact.
