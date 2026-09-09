# Architecture comparison measurements

## Active request, 2026-09-09

User authorized sustained measurements of all three candidate architectures and
measurement tools before choosing a redesign. No version migration or default
backend change. Work remains on lane/version-structure-audit, with owned Linux
worktree anvil:~/nlanes/version-structure-audit. Live hardware is a 24GiB RTX3090
and Ryzen9950X3D; compile sm_86. The old sm_120 host notes are stale. Gamer's
NVIDIA driver presently exposes no usable GPU. Do not repair/reboot it here.

Measurement tools are verify/architecture/: measure.c, prepare.c and a CPU-to-
CUDA semantic observation bridge. Outputs out/verify/architecture/. Default GPU
entry is unchanged; blaze_measure_set_split selects optional mode1 broad phases
or mode2 a separately stored player-edit continuation. The CPU wrapper composes
the same extracted functions in order. Hybrid skips full CPU rays but preserves
the reward crosshair, uses masked default-camera reset and commits returned
camera caches. Both dense and changed-page transfers are measured alternatives.

Completed validation so far: pre-extraction CPU versus extracted CPU on ordinary,
placement, fluid and deterministic-mob snapshots; CPU versus hybrid full internal
parity with repeat/reset, trained-policy action replay, and explicit early death
and boundary cases. Truncated reference artifacts fail nonzero. Synthetic bridge
checks cover110592pixels. No GPU-simulation parity or fair three-way speed result
yet. Current nn workload is actual native CUDA forward/sample, not PPO updates;
observations are64x36semantic/depth/edge, not full RGB rasterization.

Anvil machine lease: netherite-architecture-measure; renew during work and release
at completion. Full gate-profile CUDA build runs in tmux architecture-cuda-build-real;
logs/rc/start/end under out/verify/architecture/build-cuda.*. It was still in cicc
after roughly12minutes at14:34MDT. Earlier /usr/bin/time launcher failure is kept
as build-cuda-launch-error.*; no actual compiler ran in that failed attempt.
CPU and NN builds completed. Other Luminite CPU jobs are active despite our lease;
do not kill them or claim isolated timings while they run. Correctness/profile
diagnostics can proceed; comparative timing needs an uncontended interval.

NCU hardware access proved with a real metric. CPU perf counters work through
sudo -n perf and harness --perf-control/--perf-ack FIFO gates. Nsight Systems
bootstrap succeeded onAnvil: /opt/nvidia/nsight-systems-cli/2026.4.1/target-linux-x64/nsys,
bundled python in adjacent python/bin/python. hybrid32.nsys-rep and report-fact
JSON exist under outputs/profiles. CPU warm counter diagnostic excludes startup
via FIFO, but CPU co-tenancy still disqualifies speed ranking.

Remaining: finish CUDA build; compare GPU0/1/2 against CPU references; collect
same-workload CPU/hybrid/fullGPU/splitGPU sweeps including policy work, batch and
CPU-thread scaling; profile narrowed GPU entries; report tails, transfers and
memory with explicit scope. Record/compare mode mutates derived parity caches,
so those runs are validation-only. Fixed-action timing may replay actions loaded
from a receipt, with I/O before the loop. Report nominal ticks separately from
terminal partial-repeat work, and include reset cost separately.

# Minecraft version structure and native-state audit

## 2026-09-09 first-principles scope correction

The user excludes proximity to the existing implementation from the foundation
decision. The earlier preference for 1.8.9 on migration distance is superseded.
Choose the smallest authentic game contract supporting the intended speedrun
progression. Both versions permit compact C state and batched phase kernels.
1.7.10 is the parent's provisional narrower-scope choice; Fable prefers 1.8.9
for its enumerable state registry. Neither is a measured native-speed winner.
Sheep food is a concrete scope difference: 1.7 wool, 1.8 also mutton.

Current focus is architecture clarification, not an authorized full rewrite:
one faithful C transition program and GPU execution of equivalent ready work
across worlds. Preserve same-tick calls, scheduled-event order, RNG, client
state and observation/action dependencies. Do not invent next-tick delays.
The next discriminating implementation experiment is one fully integrated
player-phase split, preserving continuation state and all remaining systems,
with CPU/Oracle parity and measured end-to-end/build costs. No speedup yet.

Read the supplied Claude conversation 063ca500-3ce7-4f37-8cd1-7c6d9bfaa932,
checked its phase compiler logs and obtained fresh Fable advice. Analysis and
review are in out/verify/engine-architecture/architecture.txt, with the full
advice and checked raw-resource table alongside. General device scheduling,
aggressive stream overlap and a simultaneous version port are postponed.

## 2026-09-09 measured structural result

Recovered both private source trees and checked critical layouts against
official server bytecode. Actual section constructors measured 10,424 bytes
for ordinary skylit 1.7.10 storage versus 12,408 for 1.8.9. Both permit compact
native arrays; 1.8.9 does not allocate property maps per voxel. Parent reran
the storage measurement and reproduced every byte count.

Six official-server baseline runs completed, each saving 625 overworld chunks.
Across seeds 0/10/42, median startup wall time was 3.505 seconds for 1.7.10 and
4.512 for 1.8.9. This is Java startup evidence, not native gameplay throughput.
Static world/survival/render call maps and a native state-ownership ledger are
in out/verify/version-structure/reports/decision.txt and adjacent reports.
Private full evidence is under that path in anvil:~/nlanes/version-structure-audit.
Both stack-only server profiles completed with exit0. The earlier allocation-
instrumented 1.8.9 run timed out and remains explicitly incomplete; its timing
does not enter the baseline. Profiling includes startup, idle, save and shutdown.

Preferred candidate remains 1.8.9, based on reusable state/model architecture
and no demonstrated SoA advantage for 1.7.10. No migration occurred. Full
survival/client profiling and version-specific replay closure remain prerequisites
for promoting a switch. The existing C trainer remains the runtime direction;
host/device transfers and large CUDA compilation units need separate measurement
from any launcher or binding cost.

## 2026-09-09 requested decision evidence

Compare authentic Minecraft 1.7.10 and 1.8.9 source structures before choosing
a migration target. Recover or decompile private reference sources, trace the
world-generation and survival call chains, inventory persistent state that a
native C implementation must represent, and map it to proposed structure-of-
arrays storage. Measure relevant execution/build costs and distinguish measured
profiles from static call maps. Do not infer native porting ease from Java class
count alone. Preserve exact behavior, RNG consumption and update ordering.

The intended runtime remains native C with batched CPU/CUDA interfaces. Separate
launcher startup, per-step language-boundary/copy costs, host compilation,
device compilation and linking. Do not introduce a Python training/runtime
dependency or migrate the game version during this investigation. Existing
trace formats and tools are reusable infrastructure, not cross-version goldens.

Work is isolated on lane/version-structure-audit. Private reconstructed sources
and measurements belong under out/verify/version-structure/ on Anvil; only
small reports and evidence are copied to the Mac. Archive discovery found
mc-oracle.tar.zst, gtnh-1.7.10-mdk.tar.zst and netherite-v0-1.8.9.tar.zst in
Anvil's ~/dev/minecraft/legacy-cold/. Inspect archive contents before selective
extraction. No changes to the current simulator are authorized by this audit.

# Wooden-pickaxe policy transfer

## Measured result, 2026-09-07

The requested successful Oracle pickaxe transfer has NOT been achieved.
The experiment and evidence pipeline now run on the real path. Two valid Oracle
episodes completed 6,004 ticks each without acquiring item 270. Attempt 4 uses
the original Blaze-trained checkpoint; on its exact captured scene, corrected
Blaze CPU and closed-loop Magma both succeed in all three sampled attempts,
including the corresponding lane 0 at first-observed tick 1080. This is a
measured remaining transfer gap, not an initial-block mismatch or a claimed
success from action replay. Invalid attempts 0/1/3 remain separate: late initial
rotation, unloaded capture chunks, and transport/display interruption.

Actual outputs on the Mac are under out/verify/pickaxe-live/:
oracle-magma-10x.mp4 (50.1 seconds, 20 fps, Oracle/Magma/10x unmasked RGB);
oracle-magma-10x-jump-fixed.mp4 (10 seconds, same layout after the repair);
physics-before.tsv, physics-after.tsv; complete Oracle attempt2/attempt4 tapes,
requests, decisions and reports. The first video deliberately ends at Magma's
death at tick 1002; the non-advancing response afterward is excluded explicitly
in the terminal-prefix manifest, never padded into missing simulated time.
The post-fix video covers only the verified 200-action regression prefix.

Same-action replay found the first position/motion divergence at 169, a
0.20000004768371582-block Y difference. Java clears jumpTicks when jump is
released; player_survival.h omitted that branch. Fix 8817ca4 adds it. The
low-ceiling release/repress test fails without the fix, passes with it, and
actual replay now matches all 1,200 position/motion double values across 200
actions exactly. Full root tests on this source passed in 201 seconds; evidence
is out/verify/pickaxe-jump/final-audit/. CUDA parity for the repaired shared
physics has NOT been rerun. The earlier GPU training used pre-repair physics.

Do not equate a v2 Oracle-derived fixture with complete Java state: the native
converter imports actual blocks/light and preserves the declared empty player
header, but explicitly does not import RNG streams, entities, biomes, clocks
or scheduled updates. Those omissions and remaining inventory/observation/
render differences need investigation before claiming complete transfer.
The policy observes semantic block/depth images and scalars, not RGB, and uses
the existing recipe-crafting/container commands, not mouse-driven GUI crafting.
The RGB films show the actual rendered games and are not the policy input.

Continuation paths: Anvil ~/nlanes/pickaxe-oracle-live contains complete Oracle
captures and fixtures; ~/nlanes/pickaxe-video contains paired raw frames;
~/nlanes/pickaxe-jump-live contains the repaired CPU simulator/game and exact
matched-scene control reports. Keep these artifacts. Main host clones and
pre-existing untracked files remain untouched.
All owned training, capture and display jobs have stopped. Temporary launch
scripts were archived as command receipts and removed; generated evidence and
runnable remote builds remain available for the next investigation.

## 2026-09-07 requested outcome

Train a policy in Blaze to obtain a wooden pickaxe from an empty inventory.
Curriculum may include starts holding logs and later crafting prerequisites,
but final demonstrations start from scratch. Run the frozen checkpoint in
Magma and the real Minecraft 1.11.2 Oracle without engine-specific policy
changes. Record complete action/physics/inventory/block-change traces and
produce reviewed three-panel MP4s: Oracle, Magma, and saturated 10x absolute
RGB differences. Playback is 20 simulation ticks per second. Preserve these
user-requested videos and traces as deliverables under out/verify/.

Same-action replay establishes physics agreement, not closed-loop policy
transfer. Measure both separately. Report first divergence and exact coverage;
never call a scripted solution, selected best-of-N episode, absent frame, or
masked pixel residual perfect transfer. Declare the policy's structured input
and crafting action contract explicitly. A successful wooden-pickaxe goal is
inventory item 270, not the existing torch-chain success metric. Final examples
must have no initial resources and count ticks through crafting/placement.

Starting source: 87978c7 on isolated branch lane/wooden-pickaxe-transfer.
Live preflight: Anvil currently has an idle RTX 3090 and 83 GiB available RAM;
Gamer's NVIDIA driver is unavailable. No Oracle client listens on 25575.
Existing host main clones and other jobs remain untouched. Reuse verified
build artifacts only with matching source and hashes. Parent maps training;
read-only delegates map Oracle control/observations and transfer/trace gaps.

Progress: native goal acquisition is separated from torch curriculum metrics;
native Magma per-tick BOLR/PARY/actions/RGB capture and native Oracle CPU policy
driver are implemented. Java strict policy_lock/policy_step/policy_unlock
compiles and passed a real two-second idle/five-step clock gate. The native
parser's real-response subnormal-float rejection was fixed with a regression.
Oracle initial-pose/block-volume equality and recorded policy execution remain
required; successful transport is not transfer evidence.

Anvil training worktree is ~/nlanes/wooden-pickaxe-transfer, pinned feb35f4.
The two-phase pickaxe fine-tune completed 262144 nominal ticks, Adam16->32,
using only stage0/1 samples. Best fixed evaluation was9/9 at196608ticks; final
checkpoint fell to0/9, so retain the selected best and do not claim stable
training. Best weights: out/blaze/rl/pickaxe_v1_best.bin, SHA256
11e7eba329b4ab425c4c3f0c9b2321c1c546e79b6d57ade1e75574ba8b5a888a.
Starting chain4 checkpoint baseline was8/9 across10/11/33, and3/3 on10 in both
Blaze and Magma. The latter is fresh closed-loop competence, not pixel equality.
GPU job finished and released its lease; training outputs remain on Anvil.

Oracle/Magma worktree: ~/nlanes/pickaxe-oracle-live; owned Xvfb:2 tmux
pickaxe-display, Java tmux pickaxe-oracle, port25575, software GL. No hostmain
changes. Evidence: out/verify/pickaxe-live/. Old baseline replay matched physics
through pickaxe acquisition at761, then Blaze goal termination froze its last
three repeated ticks. Replay now disables simulator success termination while
the evaluator checks inventory itself. Also found a separate unmasked weather
timer difference fromtick1: Magma weather-off clears timers; Blaze advances
them even with effects disabled. Do not call full PARY equality established.
Live realgame images are 854x480. Final video must compare raw matched frames,
not MP4-decoded pixels, and retain all failed attempts as evidence.

# Runtime architecture repair

## 2026-09-04 follow-up: configurable training recipes

User authorized implementation of training recipe controls and explicitly
allowed isolated Gamer validation. Work starts at `6829035` on
`lane/training-recipe`; existing main clones, untracked files and older GPU
watchers remain untouched. Gamer's main clone has unrelated edits, so GPU
validation must use a separate worktree fetched from this branch.

Required: expose reward coefficients and curriculum advancement/sampling,
supported observation/history and action controls, starting-world constraints,
ordered training phases, and fixed evaluation recipes with explicit coverage
and checkpoint selection. Defaults preserve existing behavior. Unsupported RGB
or recurrent-model modes must not masquerade as working switches. Checkpoints
carry the policy observation/action contract and reject incompatible loads.

Phases inherit global settings, override explicitly listed values, and retain
the in-memory policy and optimizer while rebuilding episode state between
phases. Allocation/model-contract changes that cannot preserve training state
must fail before training starts. Phase budgets and transitions, effective
configurations, source worlds and evaluation results must be saved under out/.
Verification requires behavior tests plus actual CPU, Metal and available CUDA
training/evaluation runs, including a world-size/reward phase transition,
nondefault observation/action settings, and rejected incompatible inputs.

Implemented at `bc68460`. Parent Mac root tests passed, and an independent
fresh Anvil root audit passed in 208 seconds. Parent checked the copied
`out/verify/training-recipe-cpu/` bundle: exact HEAD, root rc=0, all recipe/world/
policy tests, and empty final Git status/diff. The audit's generated untracked
`magma/tests/test_randtick_census` binary was removed after confirming the build
created it; the pre-existing Mac binary remains untouched.

Actual example execution is
`out/blaze/rl/ppo --conf blaze/rl/recipes/curriculum.conf`, with optional
`--set backend=metal` or `--set backend=cuda`. This is a two-phase wiring example
using the shipped seed-10 world for both training and evaluation, not a trained
policy or held-out quality result. CPU evidence is
`out/verify/recipe-smoke/example_cpu.log` and
`out/blaze/rl/runs/run-GHJS85/`; Metal evidence is
`out/verify/recipe-smoke/example_metal.log` and
`out/blaze/rl/runs/run-NbYOIb/`. Both completed 128 nominal training ticks,
world size 32 then 64, and optimizer counters 2 then 4. Each phase's evaluation
covered 2/2 requested episodes. The final checkpoint SHA256 values are:

- CPU: `7877560cd3b54a39aec7c78d0350e6ec7096bc810226faede47d5ead7e99986d`
- Metal: `18063d7c269d16916216249260faf5cf44baab8df9f5e32f01ada751717f3ae8`

Additional executable checks under `out/verify/recipe-smoke/` cover nondefault
observations/actions, starting-angle jitter, matching evaluation, rejection of
an incompatible checkpoint, and prevention of overwriting the warm-start input
through the derived best output. The source input hash stayed unchanged.
`stage_starts.log` verifies loaded stages 2/3/4 while missing stage 1 remains
unavailable. Sanitized native trainer execution passed on Mac; the independent
phase, world-options and policy-contract tests also passed their sanitizer runs.

Gamer CUDA validation passed at `a899bb0` in `~/nlanes/training-recipe` on its
RTX 3090. Parent verified the copied source revision, 151 file hashes, 19 return
codes with explicit expected failures, nine complete evaluation reports, phase
events and final process/lease inventories under
`out/verify/training-recipe-gpu-bc68460/`. The directory retains its original
source label; its `revision` file identifies the final tested code. CUDA phases
completed 128 nominal ticks, 32x128x32 then 64x128x64, with optimizer counters
2 then 4 and both fixed evaluations covering 2/2 episodes. Default and nondefault
CUDA runs each completed 64 ticks; matching CUDA evaluations covered 2/2 each.
The intentional policy mismatch failed with no score. The final CUDA checkpoint
SHA256 is `e79468bff6e0213303dd7bf0cda83cec162a41d6c3c1c5674d566d7679652b75`;
it remains at the same verification path's `phase.bin` on Gamer. The checked NN
executable SHA256 is
`40f36b41611df1b41c7d9c01cf19b87f2744dbbb972bb57358057ecb48f13c6e`.

Actual runtime validation found a Linux OpenMP unload race: the evaluator's
main thread called dlclose while worker threads still executed libgomp code.
The all-thread backtrace is preserved under `first-runtime-failure/`. Fix
`a899bb0` keeps Linux environment libraries mapped until process exit in both
PPO and eval; environment destruction still frees state. The failing CPU eval
then passed three consecutive runs, and CPU two-phase execution also passed.
Final Mac root tests and native builds passed on this code; their zero return
codes and logs are `out/verify/training-recipe-mac-*-final.*`.
The independent Anvil audit on `a899bb0` also passed: root tests in 187 seconds,
native builds and CPU two-phase training/evaluation. Parent checked its exact
revision, all return codes, both complete reports, optimizer events and empty
source status/diff in `out/verify/training-recipe-loader/`.

Gamer's original CPU compiler was stopped before exhausting host memory; no
unrelated GPU job was touched. Anvil cross-compiled the unchanged environment
for sm_86 with CUDA 13.3, dev profile and scalar tick enabled. Actual make rc=0
was captured after 44m27s; a replaced timeout supervisor's separate rc=137 is
not the compiler result. Parent verified the 10,388,288-byte library SHA256
`ead855749146007f92b70b6dceab7e389ebde1bdb2a7ecd4afb5e7575345cbc0`
and the copied evidence in `out/verify/training-recipe-cuda-fallback/`.
All 200 environment/core source hashes matched before import to Gamer.
This dev build supplies functional evidence only, not throughput measurements.
All owned build/validation processes have stopped, GPU leases are released,
and one-use scripts are removed. The clean Gamer worktree retains runnable
binaries and checkpoints; earlier unrelated Anvil watchers remain untouched.

Authorized 2026-09-04 after the architecture review at `4ccbf35`.

## Follow-up: finite world size in the training recipe

User authorized a smaller configurable current world region after clarifying
that future exploration should use dynamic chunk generation. Work is isolated
on `lane/world-recipe` from `1ac24e6`; the prior Anvil GPU watcher remains pinned
to its already validated source and is not reset for this follow-up.

Required result: native `ppo.conf` / `--set world_size=N` / `--dump-config`,
default64 horizontal blocks with inherited height (128 on shipped fixtures),
0 to preserve source extent, no silent expansion. Prepare exact cropped native
snapshots and compatible bank sidecars; read curriculum targets from those
same inputs; leave originals untouched and save measured bounds/checksums and
effective recipe. World-boundary endings must be truncations with correct
final-observation bootstrap, not deaths or false curriculum captures.

Chunk streaming is future work, explicitly separate from this finite recipe.
CPU/Metal builds, actual native training, crop and bank lifecycle tests, and
boundary/reward/bootstrap checks are required before reporting this implemented.

Implemented and validated at `600dfbb` on `lane/world-recipe`. The default
produces 64x128x64 (524,288 cells) from the shipped 128x128x128 input, reducing
cell count by 75 percent. This is not a measurement of total environment memory
or training throughput. Metal observation capacity follows the actual prepared
world size, bounded by its configured ceiling.

Independent Anvil CPU audit passed root `make test`, native dimension lifecycle,
all 30 configured Magma/Blaze comparison rows and all 27 resume gates, totaling
57 comparison gates. Parent independently checked every report's status and
return code. Evidence, source revision, empty source diff and binary hashes are
in `out/verify/world-recipe-integrated-cpu/`. Mac root `make test` also passed;
its log and zero return code are `out/verify/world-recipe-mac-root.*`.

Actual native PPO training passed 32 ticks with four episode endings on Linux
CPU and Mac Metal using `--set ep_dec=1`. Linux also passed with `world_size=0`
to preserve the original fixture. These are execution/reset/update checks,
not learning-quality or throughput claims. Commands use
`out/blaze/rl/ppo --conf blaze/rl/ppo.conf --set backend=cpu|metal`
with distinct checkpoints under `out/blaze/rl/`. Logs and return codes are
`out/verify/world-recipe-linux-ppo.*`, `world-recipe-linux-inherit.*`, and
`world-recipe-mac-metal.*`.

The actual Mac prepared input is
`out/blaze/rl/worlds/run-J9dHAQ/snapshot_000.bsnp`, beside its `manifest.tsv`
and effective `recipe.conf`. It contains 415 log blocks and 2,903 coal blocks;
the original input has 1,971 log blocks. The Linux independently prepared input
at `anvil:~/nlanes/world-recipe/out/blaze/rl/worlds/run-J7XkSY/snapshot_000.bsnp`
has the same SHA256:
`6de931c790deb6fd36672fcd2edf2fc72ce5c7e50a803c90acc2f1614ffffb49`.
The Mac Metal checkpoint SHA256 is
`7291848dcc717f6572708f55c715eae07be0805355d15d5304703b67a6956867`.
The Linux CPU checkpoint SHA256 is
`4d8eda22f73f3bca1845defbbb59bd3535f2355174e95e6fed0cbf7c2cccd3dd`.

Linux native PPO and CUDA libraries compile successfully for `sm_120`, including
scalar and warp simulation, using the dev build profile. This is compilation
evidence only. CUDA execution for this follow-up remains pending: both Anvil
GPUs are occupied by unrelated training. The older architecture watcher does
not validate this new revision. Keep the branches isolated until outstanding
GPU results are reviewed; no master promotion is implied.

The follow-up watcher is Anvil tmux `world-recipe-gpu`, pinned to `600dfbb` in
`~/nlanes/world-recipe`. Its generated script is
`out/verify/world-recipe-gpu-validation.sh`; logs, heartbeat, artifact hashes,
per-gate `results.tsv` and final `exit-code` are under
`out/verify/world-recipe-gpu/`. It waits for the older architecture watcher,
then requires an idle GPU1 and the `gpu1` coordinator lease. The absolute
deadline is 2026-09-05 20:58:39 UTC, covering waiting and execution. It runs a
native CUDA PPO update with the 64-block region, the public dimension lifecycle
test, and all 30 matrix rows. The existing CPU-only `mobs_det` row remains an
explicit rc=3 blocked result; it cannot turn the full matrix green. Initial
PID 198395 was live and the waiting heartbeat was checked. A live waiter is
not CUDA validation. Keep this worktree and its artifacts until results are
reviewed, then remove the completed watcher and temporary validation lanes.

## Required result

- Native replay rejects child failures, incomplete or malformed traces, and
  measured state differences. Regression tests exercise the executable contract.
- Magma owns player-controller state per runtime. Blaze and Magma share its
  simulation declaration; rendering and audio transients remain explicit.
  Interleaved runtimes must not affect one another. Snapshot formats stay stable.
- Blaze keeps private mutable world regions for each supplied dimension across
  round trips, computes portal arrivals from world contents, and reports missing
  dimension data as an error. CPU and CUDA execute the same transfer semantics.
  Fixed snapshot regions remain the supported world boundary; missing world
  coverage must not be treated as air during portal search.
- Current architecture comments and specifications describe the implemented
  paths and their remaining coverage limits.

## Validation and integration

Linux builds and validation run in isolated Anvil worktrees based on `4ccbf35`.
Existing Anvil worktrees and GPU jobs remain owned by their current users.
Native regression tests, controller isolation, portal round trips and mutation
preservation, Magma/Blaze CPU comparisons, supported CPU/CUDA comparisons, and
root native tests must pass on the integrated tree. GPU checks require an idle
leased device. No changed tolerance or skipped evidence can close a failure.

## Integrated result and remaining validation

Implementation is pushed on `lane/runtime-architecture`, based on the reviewed
`4ccbf35` from the pre-existing `wip/nn-fable` branch. Do not merge its unrelated
base changes into master as part of this task. Anvil main was `fae41d7` at
inspection and remains untouched. Final validation uses the isolated
`anvil:~/nlanes/runtime-architecture` at source revision `30b2fb6`, including the
Mac graph-compilation and replay-declaration fixes.

Replay and controller lanes are integrated. Native replay passes 32 executable
contract cases, including sanitizer validation, and an eight-action real tape.
All 41 mutable controller variables have explicit runtime ownership. Dimension
tests pass 10,841 public-ABI checks; the strict 346-action Magma/Blaze CPU round
trip passes player, portal, dimension, world and random-tick comparisons.
Creation planning, negative coordinates, fluid scheduler retention and lava
cadence have native unit coverage. The portal tape has no active fluid evidence.

Independent CPU audit at `9959957` passed root make test and 29 configured
subsystem rows. The remaining mining row's original v1 snapshot could not
support resume because it lacked light. The matrix now uses the complete v2
seed-10 fixture: direct 1,000-tick and N=968/M=32 resume comparisons both pass.
The original fixture remains intact.

Final Linux root, lifecycle, mining (including resume) and portal gates at
`75430fe` all returned zero. Their logs and `.rc` files are under
`out/verify/architecture-final-*`, copied to the Mac for review. The independent
subsystem audit is `out/verify/integrated-9959957-summary.tsv`.
`architecture-final-cpu.sha256` records the tested Anvil artifacts:

- `magma/magma_game`: `6554e977a3dfb5f156b2cd3412776d9edeb29d63a5b2930cb8d33d829fb0433b`
- `blaze/env/blaze_cpu.so`: `47cfba67d389f2478d2823f23ab58c88c5c33596a58b6e14eb14908c95c3542e`

Final CUDA compilation passed with both scalar and warp paths using the dev
profile. This is not runtime parity or performance evidence. The CPU/CUDA/game
binary hashes are in `out/verify/architecture-final-artifacts.sha256`. Both
libraries were deleted and rebuilt again after integrating `30b2fb6`.
The final integrated audit passed root `make test` (176 seconds), the native
dimension lifecycle test, and all 30 supported CPU comparison rows with their
configured resume gates. `out/verify/architecture-integrated-cpu/summary.tsv`
records 32 zero-return checks; `exit-code` is zero. Source revision and binary
hashes were checked before and after the audit. Logs are copied to the Mac
under that same relative path.

Both Anvil GPUs are occupied by unrelated training. The bounded detached
validation job is `runtime-architecture-gpu` in tmux, using
`out/verify/architecture-gpu-validation.sh`. It waits up to 24 hours for the
final build, a passing integrated CPU audit and an actually idle GPU1, then
acquires the coordinator lease `netherite-architecture-30b2fb6`. It checks the
pinned source revision and GPU
occupancy again before execution. It runs the public CUDA lifecycle test and
all 30 supported CPU/CUDA rows, including each configured kernel and resume
check. It never stops another job.

GPU preflight at `30b2fb6` found all required artifacts for the 29 runnable
rows. `mobs_det`, the deterministic mob-AI row, is CPU-only despite its shared
`supported: true` flag: it deliberately has no CUDA command or setter. This is
a pre-existing blocked row, documented in `blaze/OPEN_DIVERGENCES.md`; porting
that AI is outside these architecture repairs. The watcher records its rc=3
instead of skipping or passing it, so even 29 clean runnable rows will leave
the full-matrix exit code nonzero. Inspect individual results and preserve
that distinction when reporting GPU coverage.

Watch `out/verify/architecture-gpu-watch.log`; results, revision and binary
hashes go under `out/verify/architecture-gpu/`. `results.tsv` records each
return code and `exit-code` records completion. A waiting process is not a
passed gate. Review every nonzero row before retrying or promotion. Retain the
validation worktree until GPU review is complete; then remove completed task
lanes and the generated watcher script.

Mac CPU and Metal game builds pass. Root make test exposed a pre-existing
Metal neural-network shutdown race: asynchronous MPSGraph compilation can
outlive process teardown. A fresh unpatched build crashed in two of five runs;
the minimal synchronous-compilation fix passed ten of ten. It is integrated in
`30b2fb6`, alongside the Mac replay-test declaration fix. Final Mac root
`make test` passed on that integrated revision in 93 seconds, including Metal
NN and all 32 replay contract cases. Evidence is copied to the main Mac checkout:
`out/verify/architecture-mac-root-final.log`, its `.rc` file (zero), and
`out/verify/architecture-metal-ten-runs.tsv`. The tested isolated Metal
executable SHA256 is
`ec13cc9b878cbbbd0866e6d1d8e429863bb531a87ff40c135f67c2d2f5fc1624`.
