# Blaze training to Java pilot interface parity audit

Date: 2026-08-02

Pilot source pin: `d55c7f01c1139299be3f7fa0b98ef11b82c3b473`

Checkpoint SHA-256: `ecd7aa73709fa9485364fda768559a7cbc45e43352ed703f5c2ead8a373266f0`

## Verdict

The pilot was not a controlled transfer test. The largest measured gap is not a
minor feature conversion: the Java player started 241.21 to 295.95 blocks from
the corresponding Blaze snapshot pose, and archived local block windows differ
in 18.40% to 24.61% of comparable cells. The second is the timebase: Blaze
advances one world tick per bridge action, while the archived Java server ran at
a median 1003.68 ticks/s and the bridge delivered 9.8 to 15.4 actions/s,
equivalent to 65.17 to 102.42 server ticks per bridge action. The pilot also
applied its intended gamerules only once across 16 fresh-world launches.

The 18-channel tensor shape, plane construction, 27-scalar packing, head decode,
action delivery, and torch success predicate are mostly identical by static
trace. Those agreements do not rescue the measurement because the policy was
placed in a different coordinate/state distribution under radically different
world dynamics.

This audit used the pinned source and archived artifacts first. It did not start
a live Java client or use a GPU. The archive has no raw policy observations, so
live Java plane/scalar distributions and the initial Java rotation remain
unverifiable from this run.

## Ranked gap backlog

### 1. Seed identity did not produce spawn-state or local block identity

- **Dimension:** episode/task semantics and observation distribution.
- **Blaze:** the snapshot baker launches `magma_game --seed X --mobs off`, not
  Java, and the magma runtime uses a fixed X/Z spawn of 8.5
  (`make_snapshots.py:84-104`, `runtime.c:292-327`). Seed 2/3/10 t0 snapshots
  place the player at
  `(8.5,79,8.5)`, `(8.5,84,8.5)`, and `(8.5,66,8.5)` respectively.
- **Pilot:** the qrl reset teleports to the vanilla world spawn plus 0.5 on X/Z
  (`Recorder.java:4596-4600`). Archived `level.dat` files give initial reset
  poses `(4.5,64,256.5)`, `(-163.5,64,248.5)`, and `(32.5,64,248.5)`.
- **Magnitude:** Euclidean pose deltas are 248.49, 295.95, and 241.21 blocks;
  horizontal deltas are 248.03, 295.27, and 241.20 blocks. Numeric
  `world_seed` matched on all 15 attempts, but no pose or terrain digest was
  checked. In relative `dx[-16,16], dy[-24,40], dz[-16,16]` windows, the
  archived final Java saves differ from the snapshots in 13,023/70,785 cells
  for seed 2 (18.40%), 16,581/70,785 for seed 3 (23.42%), and 14,991/60,905
  available cells for seed 10 (24.61%; 9,880 cells were absent from the final
  save). These saves are post-attempt, so the exact initial block mismatch still
  needs measurement. This falsifies archived state parity, not numeric seed
  identity.
- **Expected impact: high.** A convolutional navigation/mining policy was
  evaluated at a different elevation and coordinate region with a measured
  local block-state distribution mismatch.
- **Cheapest fix/gate:** put exact spawn pose and a block-volume hash into each
  snapshot manifest. On Java reset, teleport to that pose and require the same
  local block hash before policy inference. If the generators cannot match,
  create Blaze snapshots from the archived Java save rather than regenerating
  from the seed.

### 2. The integrated server advanced 65 to 102 times per policy action

- **Dimension:** action alignment, task dynamics, and runtime health.
- **Blaze:** each `env.step(..., repeat=4)` executes exactly four simulation
  ticks, one per repeated action (`blaze_core.h:2287-2298`).
- **Pilot:** every episode calls `env.overclock(1)`, which sets the independent
  integrated-server tick length to 1 ms (`qrl_chain_demo.py:128-138`,
  `Recorder.java:1380-1384`). The client bridge still processes actions on the
  client tick loop.
- **Magnitude:** 4,990 heartbeat samples yield 12 contiguous server-rate
  segments: 996.46 min, 1003.68 median, 1085.71 max server ticks/s. Eleven full
  episode samples give 9.8 to 15.4 bridge actions/s. Dividing the median server
  rate by the endpoints gives 65.17 to 102.42 server ticks per bridge action,
  versus exactly 1 in Blaze.
- **Expected impact: high.** Random ticks, mob AI, hunger, damage, item aging,
  daylight, and other server systems evolve tens of ticks between observations
  even if client movement receives one acknowledged input.
- **Cheapest fix/gate:** do not independently overclock the server. Add a
  receipt with client tick and server tick before/after every socket action and
  require both deltas to equal one. Abort the evaluation on the first mismatch.

### 3. Fresh attempts did not receive the intended world settings, and mobs were enabled

- **Dimension:** episode/task semantics and runtime health.
- **Blaze:** t0 snapshots are explicitly baked with `--mobs off`; the batched
  chain task has no daylight or weather process, and the audited t0 states start
  at tick 0.
- **Pilot:** `fast.yaml` requests frozen time 6000, clear weather,
  `doDaylightCycle=false`, and `doWeatherCycle=false`, while intentionally
  keeping `doMobSpawning=true`. `Recorder` applies the launch settings once per
  client process (`Recorder.java:1185-1196`), but the evaluator creates a new
  world for every attempt.
- **Magnitude:** the client log contains 16 fresh world launches and only one
  `launch settings applied` event. Final saves for all three seeds have
  `doDaylightCycle=true` and `doWeatherCycle=true`; their `DayTime` values are
  511224, 611014, and 23400. Four of five seed-10 attempts died after only 188,
  372, 196, and 212 actions. Blaze has zero natural-mob spawns in this task.
- **Expected impact: high.** At roughly 1000 server ticks/s, enabled mobs and
  default clock/weather dynamics can kill or perturb an agent before it makes
  meaningful progress, a transition distribution absent from training.
- **Cheapest fix/gate:** reset `launchApplied` for every fresh world, apply and
  read back gamerules/time/weather before returning reset, and use
  `doMobSpawning=false` until Blaze trains with the same mob process. Persist the
  readback in each attempt receipt.

### 4. Initial rotation was neither set nor recorded

- **Dimension:** observation and episode reset semantics.
- **Blaze:** all three audited t0 snapshots specify yaw 180 degrees and pitch 0.
- **Pilot:** the reset explicitly sets position and zeroes motion but does not
  set yaw or pitch (`Recorder.java:4596-4600`). The initial policy observation
  was not archived.
- **Magnitude:** needs measurement. The possible yaw gap is up to 180 degrees;
  the actual Java initial yaw/pitch cannot be recovered from the artifacts.
- **Expected impact: high if nonzero.** Rotation changes every visual channel
  and both coal-direction scalars on the first decision.
- **Cheapest fix/gate:** set yaw/pitch from the snapshot manifest during reset,
  archive the initial observation, and assert position, rotation, inventory,
  health, and clock before sampling action zero.

### 5. The artifact is not a valid paired sim-to-real result

- **Dimension:** campaign measurement integrity, outside the four runtime
  interfaces.
- **Blaze comparison:** a valid pair requires a real magma result from the same
  clean commit/checkpoint/protocol and at least 13 distinct seeds
  (`pair_sim2real.py:13-64`).
- **Pilot:** `java.json` reports `tracked_clean=false`; the merge receipt says
  pinned clean provenance was not preserved; `sim_stub.json` explicitly says it
  is scaffolding and not a real eval of the checkpoint. Only 3 seeds were run.
- **Magnitude:** 0 real paired-sim attempts, 3/13 minimum seeds, and 15 Java
  attempts whose merged provenance failed the strict gate.
- **Expected impact: high for campaign decisions.** The result cannot isolate
  transfer failure from checkpoint competence or harness drift.
- **Cheapest fix/gate:** refuse to publish or summarize a pilot when strict
  pairing fails. Require a real sim artifact, clean provenance, source and
  checkpoint digests, and the full seed set before Java launch.

### 6. Training reset distribution and first-decision semantics differ

- **Dimension:** episode/task semantics and observation history.
- **Blaze:** `T0_SHARE=0.30` forces 30% of curriculum samples to t0; the other
  70% enter the frontier/rehearsal sampler once captures exist
  (`ppo_chain_cu.py:74,246-288`). Every reset executes a four-tick forced-noop
  burn-in, repeats that post-burn-in frame across both stack slots, and excludes
  the burn-in transition from PPO (`ppo_chain_cu.py:425-474`).
- **Pilot:** all attempts are fresh cold vanilla spawns. The first action is
  sampled immediately from an initial duplicated frame, with no four-tick
  burn-in.
- **Magnitude:** 30% forced-t0 branch versus 100% cold-start Java; 4 Blaze
  burn-in ticks versus 0 Java burn-in ticks. The realized training fraction at
  each captured stage is not in the available artifacts and needs measurement.
- **Expected impact: medium to high.** The policy can spend most curriculum
  exposure on restored milestone states and never sees Java's exact first
  transition.
- **Cheapest fix/gate:** report the realized training start-stage histogram,
  evaluate separate t0 and frontier suites, and apply the same four-tick noop
  initialization on Java before the first archived policy observation.

### 7. The pilot preserved no observation or per-head action samples

- **Dimension:** observation/action auditability.
- **Blaze:** training tensors are available in-process and have a fixed schema.
- **Pilot:** the driver sets `FRAME_EVERY=0`; result artifacts retain only
  outcomes, best inventory, action counts, and hashes. No cam/depth/edge,
  scalar, pose, rotation, server tick, or decoded-head rows are stored.
- **Magnitude:** 0 raw policy-observation files and 0 per-head action histogram
  files across 66,968 socket actions. All 66,968 actions were counted non-noop.
- **Expected impact: medium.** It prevents the requested live distribution and
  temporal-parity gate, leaving large failures diagnosable only indirectly.
- **Cheapest fix/gate:** archive initial and then periodic pre-action/post-action
  tensors, decoded head values, client/server tick IDs, pose, and task state.
  A compact sampled JSONL or NPZ is sufficient; pixel screenshots are not.

### 8. Client-physics lockstep is intended but not proven

- **Dimension:** action tick alignment.
- **Blaze:** four action sub-ticks are unconditional and exact.
- **Pilot:** an action is applied at a client-tick END callback and its
  observation is returned on the next client tick (`Recorder.java:1212-1224,
  4693-4696`). A 20 ms poll tries to receive the next command in the same tick;
  missing it permits a free-running client tick with held keys
  (`Recorder.java:1227-1243`).
- **Magnitude:** intended 1 client-physics tick per socket step; actual value
  needs measurement. Archived bridge throughput is 9.8 to 15.4 actions/s, but
  client FPS/tick IDs were not recorded, so rate alone cannot prove the delta.
- **Expected impact: medium.** An occasional extra held-input tick changes
  movement, breaking, use edges, and frame-stack spacing.
- **Cheapest fix/gate:** return monotonically increasing client and server tick
  IDs with every action and require `+1/+1`; make the bridge scheduler truly
  step-driven instead of relying on a wall-clock poll window.

### 9. Client reset produced large exception bursts

- **Dimension:** runtime health.
- **Blaze:** no client networking/statistics subsystem exists.
- **Pilot:** `client0.log` contains 1,544 `Client thread/FATAL: Error executing
  task` events, 38 explicit stack frames at
  `NetHandlerPlayClient.handleStatistics`, and 13 `moved wrongly` warnings.
  The largest same-second fatal cluster is 598. The initial driver invocation
  also failed before launch with `ModuleNotFoundError: yaml`.
- **Magnitude:** 1,544 fatal task events and 13 movement warnings; no evaluator
  process crash is recorded, and every fragment completed. Exact gameplay-state
  impact needs measurement.
- **Expected impact: medium.** Completion and exact action hashes show the bridge
  survived, but reset-time client task loss and movement correction can alter
  observable or physical state.
- **Cheapest fix/gate:** eliminate the reset statistics NPE, treat any client
  FATAL as attempt failure, and persist position-correction counts per attempt.

### 10. Simultaneous craft and interact execute in opposite order

- **Dimension:** action pipeline.
- **Blaze:** pre-tick primitives execute craft then interact
  (`blaze_core.h:2327-2334`).
- **Pilot:** `applyAction` executes interact then craft
  (`Recorder.java:4735-4742`).
- **Magnitude:** one ordering inversion. If both heads fire while a nearby table
  is not already open, a 3x3 craft can succeed in Java and fail in Blaze on the
  same decision. The archive has no per-head trace, so occurrence count needs
  measurement.
- **Expected impact: low and biased in Java's favor.** It affects only
  simultaneous interact plus table-gated craft and cannot explain zero logs.
- **Cheapest fix/gate:** use the same primitive order and add a truth-table test
  over all craft/interact pairs with container closed/open.

### 11. Pitch clamps differ at the endpoints

- **Dimension:** action pipeline and visual observation.
- **Blaze:** clamps pitch to `[-89, 89]` (`blaze_core.h:1018-1019`).
- **Pilot:** clamps to `[-90, 90]` (`Recorder.java:4729-4731`).
- **Magnitude:** 1 degree at each endpoint; head increments and sign are
  otherwise identical at `{-10,0,+10}` degrees.
- **Expected impact: low.** Only policies that drive against the vertical clamp
  see the difference.
- **Cheapest fix/gate:** standardize the clamp and exhaustively test all three
  pitch actions from every reachable 10-degree bucket near the endpoints.

### 12. Interact candidate tie semantics are not proven identical

- **Dimension:** action pipeline.
- **Blaze:** scans/selects table/furnace candidates with float-cast player pose
  and a documented deterministic tie order (`blaze_core.h:1665-1760`).
- **Pilot:** scans the live client world with doubles in X/Y/Z loop order and
  measures from eye position (`Recorder.java:4751-4779`).
- **Magnitude:** both use a six-block reach and IDs 58/61/62; differing cases
  need measurement and require multiple near-equal candidates.
- **Expected impact: low.** The normal chain has at most one placed table nearby.
- **Cheapest fix/gate:** run a small candidate-layout corpus through both
  selectors and require the same block position and resulting container state.

## Observation pipeline

### Source and temporal path

Blaze renders a semantic 64x36 voxel raycast from the simulated world into
`cam:int16`, `depth:uint8`, and `edge:uint8`. Java does not use magma capture,
mcwindow, or a framebuffer. `qrl.SemanticCamera` raycasts the live
`WorldClient`, returns plain block IDs plus quantized depth and edge, and the
Python evaluator converts the JSON arrays to the same dtypes
(`SemanticCamera.java:8-22`, `qrl_chain_demo.py:106-125`). There is no resize or
crop on either path.

Each decision observes the result after repeat 3. Blaze renders after the fourth
simulation sub-tick. Java sends four sequential socket actions, requests camera
only on repeat 3, and forms the tensor from that returned post-client-tick
observation (`qrl_chain_demo.py:181-206`). The intended client alignment is the
same, but actual client/server tick parity is unverified as described in gaps 2
and 8.

Initial stack construction is `frame.repeat(..., STACK=2)` in both evaluator
paths. Later stacks are `[previous nine planes, current nine planes]`. The
training burn-in difference is described in gap 6.

### All 18 channels

Every entry is 36x64. `cam` is int16 before plane construction; each stored
network plane is uint8 and is converted to float32 by `obs_float`. There is no
resize or crop.

| Channel | Stack slot | Plane source | Stored range | Network scaling |
|---:|:---|:---|:---|:---|
| 0 | previous | `cam == 17` log | 0/1 uint8 | unchanged float32 |
| 1 | previous | `cam == 18` leaves | 0/1 uint8 | unchanged float32 |
| 2 | previous | `cam == 16` coal ore | 0/1 uint8 | unchanged float32 |
| 3 | previous | `cam in {1,4}` stone/cobble | 0/1 uint8 | unchanged float32 |
| 4 | previous | `cam in {2,3}` grass/dirt | 0/1 uint8 | unchanged float32 |
| 5 | previous | `cam == 58` crafting table | 0/1 uint8 | unchanged float32 |
| 6 | previous | `cam != 0` solid | 0/1 uint8 | unchanged float32 |
| 7 | previous | `floor(ray_distance*4)`, sky 255 | 0..255 uint8 | divide by 255 |
| 8 | previous | hit within 0.05 of face border | 0/1 uint8 | unchanged float32 |
| 9 | current | `cam == 17` log | 0/1 uint8 | unchanged float32 |
| 10 | current | `cam == 18` leaves | 0/1 uint8 | unchanged float32 |
| 11 | current | `cam == 16` coal ore | 0/1 uint8 | unchanged float32 |
| 12 | current | `cam in {1,4}` stone/cobble | 0/1 uint8 | unchanged float32 |
| 13 | current | `cam in {2,3}` grass/dirt | 0/1 uint8 | unchanged float32 |
| 14 | current | `cam == 58` crafting table | 0/1 uint8 | unchanged float32 |
| 15 | current | `cam != 0` solid | 0/1 uint8 | unchanged float32 |
| 16 | current | `floor(ray_distance*4)`, sky 255 | 0..255 uint8 | divide by 255 |
| 17 | current | hit within 0.05 of face border | 0/1 uint8 | unchanged float32 |

The construction and scaling are defined at `ppo_chain_cu.py:104-170` and are
imported directly by the pinned Java evaluator. This part is verified identical
by code.

### Controlled t0 plane statistics

The archive contains no live Java tensor samples. To isolate camera numeric
implementation from world mismatch, the receipt renderer ran the Blaze float32
DDA and Java double DDA over the exact same three d55 t0 snapshots and poses.
Across 6,912 pixels, there were 0 differing raw cam/depth/edge pixels and 0
differing derived plane pixels. This is a controlled code-path result, not a
claim about the archived live Java observations.

Across those three 2,304-pixel t0 frames, per-seed plane means were:

| Plane | Min seed mean | Max seed mean | Three-frame mean | Nonzero pixels |
|:---|---:|---:|---:|---:|
| log | 0.008681 | 0.109809 | 0.058304 | 403 |
| leaves | 0.114149 | 0.191840 | 0.158854 | 1,098 |
| coal ore | 0.000000 | 0.002604 | 0.000868 | 6 |
| stone/cobble | 0.000000 | 0.327691 | 0.171296 | 1,184 |
| grass/dirt | 0.275608 | 0.649306 | 0.438947 | 3,034 |
| crafting table | 0.000000 | 0.000000 | 0.000000 | 0 |
| solid | 0.621094 | 1.000000 | 0.847656 | 5,859 |
| depth, raw uint8 | 16.119358 | 123.940104 | 61.364294 | 6,912 |
| edge | 0.100694 | 0.205729 | 0.158999 | 1,099 |

Full histograms by seed and plane are in
`optloop_runs/obsparity-v1/PRESERVED/t0_snapshot_observation_stats.json`.

### All 27 scalars

All outputs are float32. Java computes scalars 0 through 5 in Python from the
bridge's nearest-32 coal list; Blaze produces the same semantic list and formula.
Inventory item IDs and scalar packing are shared through the imported
`build_scal`. Formula-level parity is verified. Numeric live-pilot parity is
unverified because the archive has no scalar rows and the worlds/poses differ.

| Index | Scalar | Source | Scaling |
|---:|:---|:---|:---|
| 0 | coal relative yaw sin | nearest coal vs eye pose/yaw | `sin(radians(relative_yaw))` |
| 1 | coal relative yaw cos | same | `cos(radians(relative_yaw))` |
| 2 | coal relative pitch | nearest coal vs eye pose/pitch | divide degrees by 90 |
| 3 | coal distance | nearest coal Euclidean distance | clip 24, divide 24; 1 if none |
| 4 | player pitch sin | player pitch | `sin(radians(pitch))` |
| 5 | player pitch cos | player pitch | `cos(radians(pitch))` |
| 6 | log count | inventory ID 17 | clip 10, divide 10 |
| 7 | plank count | inventory ID 5 | clip 10, divide 10 |
| 8 | stick count | inventory ID 280 | clip 10, divide 10 |
| 9 | cobble count | inventory ID 4 | clip 10, divide 10 |
| 10 | table count | inventory ID 58 | clip 10, divide 10 |
| 11 | wooden pick count | inventory ID 270 | clip 10, divide 10 |
| 12 | stone pick count | inventory ID 274 | clip 10, divide 10 |
| 13 | coal count | inventory ID 263 | clip 10, divide 10 |
| 14 | torch count | inventory ID 50 | clip 10, divide 10 |
| 15 | container open | semantic container state > 0 | boolean 0/1 |
| 16 | held log | held ID 17 with positive count | one-hot 0/1 |
| 17 | held planks | held ID 5 with positive count | one-hot 0/1 |
| 18 | held stick | held ID 280 with positive count | one-hot 0/1 |
| 19 | held cobble | held ID 4 with positive count | one-hot 0/1 |
| 20 | held table | held ID 58 with positive count | one-hot 0/1 |
| 21 | held wooden pick | held ID 270 with positive count | one-hot 0/1 |
| 22 | held stone pick | held ID 274 with positive count | one-hot 0/1 |
| 23 | held coal | held ID 263 with positive count | one-hot 0/1 |
| 24 | held torch | held ID 50 with positive count | one-hot 0/1 |
| 25 | player feet Y | live pose Y | divide by 64, no clip |
| 26 | episode fraction | decision index | divide by 1500 |

The pinned `build_scal` docstring incorrectly says 28; the expression and model
both use exactly 27 (`ppo_chain_cu.py:107-112,137-147`). Controlled t0 scalar
vectors for all three snapshots, including the empty inventory/container/held
state, are preserved in the statistics receipt.

## Action pipeline

The policy has independent categorical heads of sizes
`[3,3,3,2,2,2,7,2,10]`. Both training and pilot sample each head from
`Categorical(logits)`. The pilot's exact mapping is at
`qrl_chain_demo.py:162-205`; Java application is at
`Recorder.java:4702-4748`.

| Head | Categories | Blaze application | Java application | Status |
|:---|:---|:---|:---|:---|
| dyaw | -15, 0, +15 degrees | repeat 0 | repeat 0, add to yaw | identical |
| dpitch | -10, 0, +10 degrees | repeat 0, positive down | repeat 0, positive down | identical except clamp |
| forward | -1, 0, +1 | signed input repeats 0..3 | back/forward key repeats 0..3 | identical decode |
| jump | 0/1 | held repeats 0..3 | key held repeats 0..3 | identical decode |
| attack | 0/1 | held repeats 0..3 | key held repeats 0..3 | identical decode |
| use | 0/1 | held repeats 0..3 | key held repeats 0..3 | identical decode |
| craft | none, primitives 0..5 | semantic primitive once | same recipes/table gating, repeat 0 | feasible; order gap |
| interact | 0/1 | nearest table/furnace semantic state | same mod-state operation, no GUI | feasible; selector edge cases unverified |
| hotbar | none, slots 0..8 | selection supplied each sub-tick | selection repeat 0 only | effectively identical while persistent |

`REPEAT=4` is structurally identical: camera changes and semantic primitives are
on repeat 0, persistent controls are held for all four, and the camera is
requested on repeat 3. The major non-parity is the actual world/client/server
tick count, not the Python head decode.

Action delivery itself is verified: all 15 attempts have
`bridge_action_seq == actions_sent` and local FNV equals bridge FNV, covering all
66,968 socket actions. This proves the exact serialized action objects reached
`applyAction`; it does not prove tick parity or preserve head histograms.

## Episode and task semantics

- **Seeds:** numeric Java `world_seed` equals requested seed in 15/15 attempts.
  Spatial state parity is false, as quantified in gap 1.
- **Inventory:** all audited Blaze t0 snapshots have zero nonempty inventory
  slots. Java starts fresh survival saves, and the first/best reported inventory
  is empty except one seed-3 attempt that reached two logs. Exact pre-action
  Java inventory tensors were not archived.
- **Time/weather:** intended Java time/weather was deterministic only for the
  first launched world; subsequent fresh worlds retained default cycling. Blaze
  restored snapshot task state and exposes no visual light channel, but the
  server dynamics still differ.
- **Mobs:** Java enabled vanilla natural mob spawning; the training task did not.
- **Episode horizon:** both label the horizon 6000 repeated actions, or 1500
  decisions, but Java's world experienced tens of server ticks per labeled
  action. The nominal count is identical and semantic duration is not.
- **Success:** verified identical. Blaze sets success when inventory item ID 50
  increases above reset count (`blaze_core.h:2396-2401`). Java stops when live
  `inv_counts[torch] >= 1` and checks that against its milestone result
  (`qrl_chain_demo.py:216-228`).
- **Milestones/waypoints:** Java imports the same `stage_of_best` implementation:
  logs3 or first planks, wooden pick, wooden pick plus cobble3, coal, then torch
  success. The observed best-inventory aggregation matches training's
  best-so-far definition. Reward shaping itself is training-only and is not
  expected during Java evaluation.
- **Reset:** Blaze restores t0 or captured curriculum snapshots and performs a
  four-tick burn-in. Java deletes and regenerates a fresh vanilla save for each
  attempt, teleports to vanilla spawn, and samples immediately.

## Runtime health

The client did not crash during the accepted run: all 15 fragments completed,
all action sequence/digest receipts matched, and the artifact contains 66,968
actions. That is the positive health result.

The run was not healthy enough for a parity claim:

- 996.46 to 1085.71 measured server ticks/s versus 9.8 to 15.4 bridge actions/s.
- 1,544 client-thread FATAL task-execution events, with 38 explicit
  `handleStatistics` stack frames.
- 13 `moved wrongly` warnings.
- 16 fresh launches but one world-settings application.
- Four explicit early deaths on seed 10.
- One initial orchestration failure due to missing PyYAML before the successful
  launch path.
- No archived FPS, client tick IDs, chunk-ready timings, or raw observations.
  Frame-rate versus tick-rate and chunk-load stalls therefore need measurement.

## Verified identical, falsified, and unverified matrix

**Verified identical by code and/or artifact:** 36x64 resolution; no resize or
crop; `cam:int16`, `depth:uint8`, `edge:uint8`; all 18 channel definitions and
float scaling; all 27 scalar packing formulas and item IDs; `STACK=2`; head sizes
and sampled policy mode; yaw/forward/jump/attack/use decode; nominal repeat of
four; 66,968/66,968 action delivery receipts; 15/15 numeric seed receipts;
torch success predicate and milestone function. Controlled same-snapshot camera
numeric test: 0/6,912 raw pixels differed.

**Falsified by measurement:** spawn pose; archived final local block state;
server-tick/action ratio; world-setting application on fresh attempts;
gamerule state in final saves; natural-mob parity; clean paired-result
provenance.

**Unverifiable from artifacts:** actual initial Java local block hash; live Java
plane histograms and scalar distributions; initial Java yaw/pitch; per-head
action distributions and craft/interact co-occurrence; exact client physics
ticks per action; per-attempt
server tick deltas; initial health/food/inventory tensor; FPS; chunk-ready
latency/stalls; behavioral effect of the FATAL task exceptions; exact
interact-selector edge-case parity. These need a new instrumented pilot, not a
reinterpretation of the existing receipt.

## Preserved receipts and reproduction

- `optloop_runs/obsparity-v1/PRESERVED/observation_schema.json`: all channels
  and scalars.
- `optloop_runs/obsparity-v1/PRESERVED/action_schema.json`: all heads, repeat
  protocol, and action-order gap.
- `optloop_runs/obsparity-v1/PRESERVED/t0_snapshot_observation_stats.json`:
  snapshot headers, spawn deltas, full plane histograms, controlled camera
  comparison, and t0 scalar vectors.
- `optloop_runs/obsparity-v1/PRESERVED/pilot_runtime.json`: outcome, action
  receipt, log-rate, death, warning, exception, and launch counts.
- `optloop_runs/obsparity-v1/PRESERVED/source_manifest.json`: source/artifact
  paths, sizes, SHA-256 digests, and observation-capture limitation.
- `optloop_runs/obsparity-v1/PRESERVED/parallel_java_eval_annotated.py`: exact
  driver copy with comments marking parity blind spots.

Reproduce the offline receipts from this worktree with:

```bash
UV_CACHE_DIR=/home/infatoshi/.cache/uv \
TMPDIR=/home/infatoshi/dev/nw/.tmp \
uv run --no-project --with numpy==2.5.1 --with torch==2.13.0 \
  python blaze/rl/transfer_audit/collect_obsparity_receipts.py \
  --out optloop_runs/obsparity-v1/PRESERVED
```

No live client or GPU is required.
