# Product gates (netherite)

**netherite** is the product name: a from-scratch C/CUDA reimplementation of
Minecraft 1.11.2 (magma + blaze), bit-verified against the real Java game,
plus a batched CUDA RL env (blaze). Humans play the oracle; tapes record; magma
replays; every divergence is a bug with a repro.

Agent entry: root `AGENTS.md`. How to run: `docs/RUNBOOK.md`.

## The four ship gates

### Gate 1 - game quality
Accept: a human plays magma spawn -> End with zero divergences; canonical tapes replay
bit-exact (physics tol 1e-9, pixels day-bit-exact / isolated star pixels at night, no
unexplained diff clusters).
Status: OPEN, machinery green. The physics canonical tape
(`verify/tapes/20260721T215812Z_..._77b5b462.jsonl`, 3,617 ticks, bot-recorded)
replays with no physics divergence end-to-end; the 12k human tape holds the pixel
baselines (A 0.96/ch, B 1.66/ch). Remaining bugs live in
`magma/OPEN_DIVERGENCES.md`; the full spawn->End human session has not yet been
played clean. Verification procedure: `magma/VERIFY.md`.

### Gate 2 - RL
Accept: spawn -> torches learned with ZERO scripted stages in the batched CUDA env
(blaze), the policy transfers to real magma, and a one-time JVM demo runs.
Status: OPEN, core accept MET (2026-07-15): the full spawn->torch chain is learned with
zero scripted stages (PPO in blaze, milestone reward + stage-gated dense shaping,
frontier curriculum from the policy's OWN captured states via blaze_capture). Real-env
cold-spawn transfer, sampled best-of-5: full chain on 11/13 seeds INCLUDING both
held-out seeds (11, 33); the 2 misses (2, 16) reach cobble with a self-crafted pick and
fail at finding coal. ~1.92B env-ticks / ~7.9 h active train. Net: rl/out/chain_net_cu.pt
(untracked). JVM demo SHIPPED (2026-07-15): a bit-matching Java SemanticCamera in
NetheriteMod (mod id qrl; obs_camera.h geometry) + float look / craft:N / interact bridge extensions let
the same net drive the real JVM game scriptlessly to cobble3 (log -> planks -> sticks
-> table -> wooden pick -> 45 cobble at y=22); best-of-6 sampled, video delivered.
Transfer is attenuated vs magma (biome/tree ids outside the trained cam vocabulary;
JVM terrain dynamics) - documented in DEVLOG-adjacent agent report, not a fidelity bug.
Fidelity machinery green at HEAD: blaze CPU is byte-exact vs the real env on the gated
obs fields including the full 2058-action spawn-to-torch chain tape (verify_cpu.py
--chain), CUDA is bitwise == CPU on the full 12-action ABI incl. craft/interact
(verify_cuda.py default/--chain/--mixed), vec env bit-exact vs the per-env loop
(test_vec_env.py).
Net-of-record update (2026-07-17): the v2 recipe's BEST checkpoint
rl/out/chain_net_cu_v2.pt (safety copy chain_net_record.pt) scores 12/13
full-chain on the sampled 13-seed real-env eval (both held-out seeds 5/5; sole
miss = seed 16 at coal), superseding the v1 net's 11/13. IMPORTANT: evaluate
{name}.pt (best-on-t0), never {name}_last.pt - the same leg's tail checkpoint
scores only 10/13 (see the gate-3 collapse hazard). Independent confirmation of
the recipe tonight: a 1.35-h from-scratch run's best checkpoint
(chain_net_pin1_best.pt) ties v1 at 11/13 with roughly half of v1's 1.92B ticks.

### Gate 3 - perf pins
Accept: >=1M env-ticks/s FULL-FEATURE at N>=8192; spawn-to-torch trains <1h; magma
60 fps at 1080p.
Status: throughput pin MET (2026-07-15). Full-feature t0 (128^3 regions, full 12-action
decode) on the idle RTX PRO 6000: 1.02-1.03M env-ticks/s at N=9216 (0.94M at N=8192;
legacy same-GPU A/B 0.37M). The fix stack: ore-list CSR bucketing (338fd04) + warp-
cooperative cu_recenter (7778cd9, merged fada44c) - the old kernel had one lane doing
~150k serial memory ops per chunk crossing while its warp stalled; now the warp ballots
crossing envs and all 32 lanes stride each refill. Both bit-identical, full ladder green
on sm_86 and sm_120 (default/chain/mixed CUDA gates + CPU 8-stream/chain zero-diff).
Mining slice: 0.90-1.98M at N=4096-16384 (pre-H numbers; H adds 1.2-1.4x there).
Legacy kernel kept behind load-time BLAZE_LEGACY_RECENTER=1 (zero tick cost).
Re-confirmed at HEAD 2026-07-17: 1.02M env-ticks/s at N=9216.
2026-07-19 update: 4.06M env-ticks/s full-feature t0 at N=8192 (4x the pin).
Three stacked wins, each gated byte-exact: interact container-list (k_tick
32.2 -> 15.6 ms/step), float32 DDA obs camera (k_obs 7.09 -> 1.03; ids
differ 1 px per 1.66M rays vs double), warp-per-env k_tick (BLAZE_WARP_TICK,
default on; k_tick 15.3 -> 6.7). Trainer end-to-end 0.256 -> 0.43M t/s: the
pin-budget 0.95B-tick run now takes 37 min (best t0 0.325, real-env transfer
7/13 incl. held-out s11) vs the 60-min pin1 leg. A 1h/1.53B-tick leg ran
~600M past the peak and reproduced the collapse (best 0.100) - budget by
TICKS (~0.9B), not wall-clock, and per-chunk ent/kl/clip telemetry is now
printed by ppo_chain_cu.py to diagnose the collapse next long run.
Train pin MET (2026-07-17): from scratch, zero scripted stages, v2 recipe
(spec-driven reward_chain.py, sidecar JSON per checkpoint), N=6144 on the idle
RTX PRO 6000: 0.254-0.261M env-ticks/s end-to-end, 0.90B ticks in 3600s ->
trailing t0 full-chain 0.360, best-window 0.420. The enabler was an obs_float
prealloc leak fix (ragged-minibatch shapes minted a pinned buffer per distinct
dim0; one growable buffer + [:n] view stopped the growth that OOMed N=6144+
configs and lifted sustained trainer throughput from 0.069M to 0.24-0.26M t/s).
Real-env sampled transfer of the 1h checkpoint (best-of-5 x 6000 ticks, 13
seeds): 8/13 torches incl. held-out 11; misses are 3 coal + 2 cobble3. v1
quality (11/13) needs >1h. Artifacts: rl/out/chain_net_pin1_1h.{pt,json}
+ chain_curve_pin1_1h.{npy,png}, train log pin1_train.log.
Known hazard (2026-07-17, both legs reproduce): past the ~0.4 t0 peak,
continued PPO training collapses full-chain rate to ~0.05 within ~30-40M
ticks (v2 leg fell 0.42->0.27 over its back half; pin1 continuation fell
0.39->0.05 by chunk 240). The trainer's best-on-t0 checkpoint ({name}.pt)
preserves the peak state; _last is the regressed tail. Cause undiagnosed
(entropy/KL telemetry is not printed per update) - evaluate the BEST
checkpoint, never the tail.
fps pin NOT met (re-measured 2026-07-27, MAGMA_BENCH env-gated 12-stage
timers, still cam, vd=8, SDL dummy video, 600 measured frames at 1080p):
CPU backend 4.51 fps (was 5.00 on 07-17; the cutout-coverage fix added real
geometry); RTX PRO 6000 CUDA backend 35.93 fps mean / 35.98 p50 (was 24.67
on 07-17 - the raster stage fell 28.6 -> 17.5 ms). Stage means now: raster
17.5, hud 4.4, mesh 2.3, present 1.3, cuda in+out 1.7, sky 0.43 ms. 60 fps
needs a ~1.6x raster kernel (bit-parity-constrained) plus hud caching and
io/present overlap. The 3090's 11.74 (07-17, host-rendered sky) was not
re-measured - it has a co-tenant. CPU backend is not on the 60 fps path.

2026-08-07 full-parity 70% checkpoint: the clean regression guard remains
above all frozen floors at 5,094 CPU scalar steps/s, 2.87M Blaze environment
ticks/s, and 31.05 CUDA fps. This confirms no checkpoint regression; it does
not close the separate 60 fps ship pin. Evidence:
`c/magma/trace/out/perf_guard_70pct_checkpoint.json`.

2026-08-10 survival/item-lifecycle checkpoint: the focused real-Java/native
battery passes more than 2,400 exact rows across the complete 392-item census,
block harvest/drop/tool/XP, equipment enchantments, Mending, anvil, bow/arrow,
Elytra wall damage, XP bottles, death/loot/XP, zombie-villager cure, player
pickup, and item merge. A further 39-row comparator locks flowing/still water,
lava, periodic/contact fire, wet extinguish, cactus, expiry, void retirement,
and five-direction full-cube push-out. The capsule continues air, ground,
landing, water, fire, cactus, push-out, and loaded-order merge with hidden
environment/RNG state. The clean native runtime aggregate passes in 7:42.22 at
505,560 KiB peak RSS with zero major faults and zero swap. The stopped-oracle
CPU guard passes at 4,983 scalar steps/s against the 3,858.9 floor and 4,062
baseline. All thirteen locally available quick-sweep stages pass in 1:07.91 at
715,596 KiB; only the two undistributed `.bsnp` stages skip. GPU 1 was shared
and not executed. Evidence:
`magma/trace/out/perf_guard_item_environment_cpu.json`.

2026-08-10 Evoker spell/Vex checkpoint: the direct real-Java/native comparator
matches all 16 attack-fang positions and warmups by raw bits, fang damage and
retirement boundaries, three seeded Vex positions/bounds/lifetimes, and Wololo
conversion. Focused scheduler/view, entity geometry/UV/winding, audio,
mansion, mob, item-render, and runtime regressions pass. The stopped-oracle
CPU guard passes at 4,566 scalar steps/s against the frozen 3,858.9 floor and
4,062 baseline. Every locally runnable quick-sweep stage passes, with only the
two undistributed `.bsnp` stages skipped. GPU 1 was shared and not executed.
Evidence:
`magma/trace/out/perf_guard_evoker_spells_cpu.json`.

2026-08-10 Guardian/ocean-monument checkpoint: direct real-Java/native gates
match Guardian and Elder Guardian attributes, laser target/damage boundaries,
thorns, and every seeded loot row. Five monument fixtures match candidate
chunks, randomized room graph, four-facing chunk-clipped block volume hashes
and censuses, and all three elder sites. The live seed-0 fixture discovers
chunk `(-75,-368)`, places 15,678 prismarine, 126 sea lanterns and eight gold
blocks, and materializes exactly three persistent elders without duplication.
ASAN reports no invalid access or leak in the five-fixture generator run. The
focused runtime and every locally runnable quick-sweep stage pass; only the two
undistributed `.bsnp` stages skip. CPU throughput passes at 4,438 scalar
steps/s against the 4,062 baseline and frozen 3,858.9 floor. GPU 1 was shared
and not executed. The broad aggregate passes the new gates and every unchanged
gate through `dragon_live`; its final route reproduces the pre-checkpoint
`35aa47c` harness failure because it predicts a gravel drop from coordinates
instead of the live World.rand cursor. Evidence:
`magma/trace/out/perf_guard_guardian_monument_cpu.json`.

2026-08-11 finalization audit: the simulation/save boundary adds Giant Zombie,
all 34 plain native fresh-NBT living classes, a 29-orb plus Item loaded-order
case, and an exact 20-tick active-villager continuation. Direct Java/native
fixtures cover villager mating, doors, social/farming/follow-golem behavior and
Iron Golem core state; bounded product gates cover village golem and siege
lifecycle. Core HUD is bit-exact on all nine promoted states, both inside-block
overlays are full-frame exact, and eight visible inventory interaction states
plus close behavior are exact. Java capture validity is green for all 16
focused entity/particle states. As of 2026-08-14, all 16 have mutation-tested
same-scene bounded gates: small fireball is within four channels everywhere,
grass dig and XP have zero hard pixels, and the remaining fixed-function,
dissolve, registration, and edge tails are individually classified and
ceilinged. They are not yet at the Java A/B noise floor.
First-person bow/eat/shield poses, portal composition, underwater composition,
and the inventory preview also remain strict red pixel gates. Consequently
Gate 1 remains OPEN and the project is not claiming universal pixel parity.
The four-seed wrapper census, clean native suite, Java build, focused village
gates, capsule selftest, and all 13 locally runnable quick-sweep stages pass;
the two undistributed `.bsnp` stages skip. The clean full native suite reaches
fresh spawn through credits in 9:59.65 at 1,101,216 KiB peak RSS with zero
swap. The quick sweep passes in 1:07.31 at 715,556 KiB peak RSS. CPU throughput
is 4,850 scalar steps/s, above the 4,062 baseline and frozen 3,858.9 floor.
GPU 1 was shared and not executed, so this audit neither updates nor closes the
separate 60 fps Gate 3 pin.

2026-08-07 strict-equivalence checkpoint: the full native runtime aggregate
passes in 6:45.71 with 431,540 KiB peak RSS and zero swap. The fresh CPU guard
passes at 4,204 scalar steps/s against the frozen 3,858.9 floor. GPU 1 had a
co-tenant, so no new GPU number was taken for this checkpoint.

2026-08-07 firework-audio checkpoint: the exact-current full native runtime
aggregate passes in 6:40.65 with 448,124 KiB peak RSS, zero major faults, and
zero swap. The CPU guard passes at 4,132 scalar steps/s against the frozen
3,858.9 floor. The direct real-Java/native audio comparator passes eight
boundary cases and its delay negative control. Every locally available quick
sweep stage passes; the two snapshot-backed Blaze stages skip because their
`.bsnp` inputs are absent. GPU 1 was not executed.

2026-08-07 block-break-audio checkpoint: the exact-current full native runtime
aggregate passes in 6:44.47 with 446,676 KiB peak RSS, zero major faults, and
zero swap. The CPU guard passes at 4,085 scalar steps/s against the frozen
3,858.9 floor. The exhaustive real-Java/native comparator matches all 235
registered non-air block IDs, twelve material families, every valid metadata
state, and raw volume/pitch bits, while rejecting its material negative
control. Every locally available quick-sweep stage passes; the same two
snapshot-backed Blaze stages skip because their `.bsnp` inputs are absent.
GPU 1 was not executed.

2026-08-07 block-placement-audio checkpoint: the exact-current full native
runtime aggregate passes in 6:39.86 with 437,400 KiB peak RSS and zero major
faults. The CPU guard passes at 4,142 scalar steps/s against the frozen
3,858.9 floor. The exhaustive real-Java/native comparator matches all 235
registered non-air block IDs, twelve break and twelve placement families,
every valid metadata state, and raw volume/pitch bits, while rejecting
independent material negative controls. The clean product, JDK 8, OpenAL, and
focused seven-family parity gates pass. Every locally available quick-sweep
step passes; the two snapshot stages skip because their `.bsnp` inputs are
absent. GPU 1 was not executed.

2026-08-07 block-hit-audio checkpoint: the exact-current full native runtime
aggregate passes in 6:22.74 with 448,576 KiB peak RSS, zero major faults, and
zero swap. The CPU guard passes at 4,197 scalar steps/s against the frozen
3,858.9 floor. The exhaustive real-Java/native comparator matches all 235
registered non-air block IDs, twelve break, placement, and progressive-hit
families, every valid metadata state, and raw volume/pitch bits, while
rejecting independent per-action material negative controls. The controller
gate pins damage-update cadence at zero and every fourth update; clean native,
JDK 8, OpenAL, and focused parity gates pass. Every locally available quick
sweep step passes; the two snapshot stages skip because their `.bsnp` inputs
are absent. GPU 1 was not executed.

2026-08-08 player-landing-audio checkpoint: the exact-current full native
runtime aggregate passes in 6:30.95 with 449,568 KiB peak RSS, zero major
faults, and zero swap. The CPU guard passes at 4,353 scalar steps/s against the
frozen 3,858.9 floor. The exhaustive real-Java/native comparator matches all
235 registered non-air block IDs, twelve fall families, every valid metadata
state, and raw volume/pitch bits. Focused tests pin small/big selection, hay's
0.2 damage multiplier, and ordered player/material events. Every locally
available quick-sweep stage passes; the two snapshot-backed Blaze stages skip
because their `.bsnp` inputs are absent. GPU 1 was not executed.

2026-08-08 player-water-entry-particle checkpoint: the exact-current full
native aggregate passes in 6:29.09 with 450,556 KiB peak RSS, zero major
faults, and zero swap. The CPU guard passes at 4,088 scalar steps/s against the
frozen 3,858.9 floor and above the 4,062 baseline. The real-Java/native
comparator matches all 26 ordered particle call argument bit patterns and the
final Entity.rand cursor. Focused runtime and live-particle tests pin current-
tick clearing, vanilla identities/texture cells, spawn-frame timing, lighting,
motion, gravity, and billboards. Clean Java/native builds and every locally
available quick-sweep stage pass; the two snapshot-backed Blaze stages skip
because their `.bsnp` inputs are absent. GPU 1 was not executed. Java's
wall-clock-seeded particle-constructor entropy remains outside this exact gate.

2026-08-08 player-footstep-audio checkpoint: the exact-current full native
runtime aggregate passes in 6:24.97 with 450,656 KiB peak RSS, zero major
faults, and zero swap. The CPU guard passes at 4,055 scalar steps/s against the
frozen 3,858.9 floor and is within 0.2% of the 4,062 baseline. The exhaustive
real-Java/native comparator matches all 235 registered non-air block IDs,
twelve step families, every valid metadata state, and raw volume/pitch bits.
Focused tests pin tick-10 cadence, snow override, sneak/riding suppression,
and live event position/category/scalars. Every locally available quick-sweep
stage passes; the two snapshot-backed Blaze stages skip because their `.bsnp`
inputs are absent. GPU 1 was not executed.

2026-08-08 player-swim/splash-audio checkpoint: the exact-current full native
runtime aggregate passes in 6:06.00 with 450,080 KiB peak RSS, zero major
faults, and zero swap. The CPU guard passes at 4,312 scalar steps/s against the
frozen 3,858.9 floor and exceeds the 4,062 baseline. The real-Java/native
comparator matches raw swim/splash volume and pitch bits, the volume cap,
splash's 67 total RNG draws, and the chained next-swim pitch. Focused runtime
tests pin first-entry detection, ordered pre-move splash/post-move swim sources,
category, scalars, and the final client Entity.rand cursor. Every locally
available quick-sweep stage passes; the two snapshot-backed Blaze stages skip
because their `.bsnp` inputs are absent. GPU 1 was not executed.

2026-08-08 player attack/sweep checkpoint: the exact-current full native
runtime aggregate passes in 5:45.65 with 450,268 KiB peak RSS, zero major
faults, and zero swap. The CPU guard passes at 4,482 scalar steps/s against the
frozen 3,858.9 floor. The locked real-Java fixture passes 60 cases covering
the attack predicate, damage/motion/fire state, all six player attack sounds,
movement-gated sweep selection, and Sweeping Edge I/III raw-float outcomes.
Focused native tests cover live sound order, shifted world origins, secondary
damage, and rejected attacks. OpenAL validates 146 events and 469 owned
variants. Every locally available quick-sweep stage passes; the two
snapshot-backed Blaze stages skip because their `.bsnp` inputs are absent.
GPU 1 was not executed.

2026-08-08 player attack-particle checkpoint: the locked real-Java attack
fixture passes 67 cases and now pins critical, enchantment-critical, sweep,
and damage-indicator particle identity, order, count, target geometry, and raw
spawn bits. Focused runtime, fixed-pool lifecycle, byte-exact asset, render,
clean Java, and clean native gates pass. Java's unsaved constructor entropy is
explicitly outside the exact pixel claim. The full native aggregate passes in
6:45.89 with 450,356 KiB peak RSS, zero major faults, and zero swap. The CPU
guard passes at 4,004 scalar steps/s against the frozen 3,858.9 floor and is
1.4% below the 4,062 reference baseline. All 13 locally available quick-sweep
stages pass; the two snapshot-backed Blaze stages skip because their `.bsnp`
inputs are absent.
GPU 1 was not executed.

2026-08-08 player target-audio checkpoint: the locked real-Java attack fixture
passes 79 cases, including exact pig/cow/sheep/chicken hurt and death sounds,
private-RNG pitch bits, cow volume, target coordinates, rejected and accepted
delta silence, sprint order, and sweep-neighbor order. The focused native
runtime gate passes 15 cases and OpenAL still validates all 146 events and 469
owned variants. The full native aggregate passes in 5:34.62 with 450,204 KiB
peak RSS, zero major faults, and zero swap. The stopped-oracle CPU guard passes
at 4,420 scalar steps/s against the frozen 3,858.9 floor and exceeds the 4,062
baseline. All 13 locally available quick-sweep stages pass; the two
snapshot-backed Blaze stages skip because their `.bsnp` inputs are absent. GPU
1 was not executed.

2026-08-08 potion player-return checkpoint: a locked real-Java/native oracle
matches the five-transition `EntityThrowable` path from initial nearby-thrower
suppression through ignore countdown 2, 1, 0, release at -1, and a returning
Strong Harming direct player impact. Potion removal, player health bits,
`hurtTime=10`, and `hurtResistantTime=20` match. The native brewing/runtime
suite passes 389 checks, the Java and product builds pass, and the full native
aggregate passes in 5:21.18 with 450,764 KiB peak RSS, zero major faults, and
zero swap. The stopped-oracle CPU guard passes at 4,750 scalar steps/s against
the frozen 3,858.9 floor and the 4,062 baseline. All 13 locally available
quick-sweep stages pass; the two snapshot-backed Blaze stages skip because
their `.bsnp` inputs are absent. GPU 1 was not executed.

2026-08-08 potion/cloud capsule checkpoint: Java authoritative capture now
promotes the bounded default `EntityPotion` and `EntityAreaEffectCloud` scalar
subsets into the version-2 neutral capsule. The emitter restores potion item,
type, age, motion, player thrower/ignore countdown, cloud lifecycle/radius,
player deadline, owner, entity IDs, and Java loaded-list order. A locked real
Java capture restores into native and matches the next tick at double/float bit
precision. Malformed ignored-thrower and undersized-cloud states are rejected.
The focused capsule and 389-check brewing gates pass. The full native aggregate
passes in 5:40.59 at 450,756 KiB peak RSS with zero major faults and zero swap.
The stopped-oracle CPU guard passes at 4,903 scalar steps/s against the frozen
3,858.9 floor. Every locally available quick-sweep stage passes; only the two
snapshot-backed Blaze stages skip because their `.bsnp` inputs are absent. GPU
1 was not executed.

2026-08-14 custom potion/cloud payload checkpoint: the live throw/use path,
runtime fixtures, authoritative recorder, state capsule, script restore, and
native state stream now carry up to 16 ordered custom effects with exact
amplifier/duration and ambient/particle flags, custom color, and arbitrary
thrown-stack NBT. Lingering conversion copies the payload and applies custom
cloud durations without the base PotionType quartering. Drinkable potions use
the same ordered base-plus-custom effect path and return the exact glass bottle.
The focused native brewing gate passes 415 checks. A parked real-Java/native
returning-impact gate matches thrower release, direct Strong Harming, a custom
instant heal, hidden ambient Fire Resistance, damage timers, raw health bits,
custom Strength drink completion, effect flags/duration, and bottle return. The independent
Java-to-capsule-to-native gate matches custom splash and cloud payloads,
semantic NBT, motion, RNG, UUID, colors, flags, and every represented scalar
through the next tick at raw float/double precision.

2026-08-14 area-effect-cloud extended-state checkpoint: the authoritative
capture, neutral capsule, script restore, native state stream, and live tick now
retain `DurationOnUse`, ignore-radius state, all 49 particle IDs, and both
particle parameters. A nondefault Java-authored cloud matches every represented
scalar through the next native tick. Four locked Java client-branch cases cover
active/waiting spell particles plus parameterized item/block particles; every
spawn position, velocity, parameter, count, and private entity-RNG cursor is
bit-exact. The focused native brewing gate passes 415 checks.

2026-08-14 area-effect-cloud kinematics checkpoint: the exact cloud subset no
longer requires zero motion. Authoritative capture, neutral capsule, script
restore, tick, and native state output retain all three nonzero motion doubles
plus current and previous yaw/pitch floats. A locked real-Java fixture resumes
that state and matches every field after one tick at raw bit precision.

2026-08-14 area-effect-cloud deadline-graph checkpoint: authoritative capture
now retains the complete bounded player/represented-living reapplication map,
canonicalized by target EID. Capsule restore defers graph edges until all
living targets exist. At a real Java five-tick scan boundary, an expired player
deadline is removed while a future cow deadline remains and prevents effect,
radius-on-use, and duration-on-use changes; native matches the full next state.

2026-08-14 area-effect-cloud owner-identity checkpoint: cloud UUID plus the
null/player/represented-living owner EID and UUID edge now round-trip through
authoritative capture, capsule validation, deferred graph restore, tick, and
native output. The locked fixture assigns the cow as owner and matches all
identity bits after one tick.

2026-08-14 potion indirect-damage attribution checkpoint: a locked 15-case
real-Java/native gate covers player, pig, witch, and zombie victims with cow,
player, and null owners. It matches nullable owner credit, revenge target,
recent-player timers, hurt immunity and accepted-delta behavior, armor and
Resistance reduction, exact health and motion bits, grounded knockback caps,
near-zero self-owner Math.random fallback, target RNG, sounds, and death state.
The live splash and lingering-cloud callers retain the resolved owner through
the same path. Fresh player hits additionally match the real packet
constructor's clamped 1/8000 velocity components, and the end-of-tick tracker
applies them to the client while retaining exact server motion. Rejected and
accepted-delta hits emit no packet. The focused brewing/runtime gate passes
448 checks. General non-potion damage-source attribution and acknowledgement,
statistics, and criteria remain open.

2026-08-14 Nausea/portal client-state checkpoint: the nine-case
`magma/trace/test_nausea_portal.py` gate invokes the real
`EntityPlayerSP.onLivingUpdate` and `removeActivePotionEffect` paths under a
parked server and matches native float bits exactly. It covers physical portal
precedence, the 0.0125 and 0.006666667 ramps, duration-60 decay boundary, both
clamps, explicit removal reset, and the ordinary versus Nausea phase choice.
Capture and interactive composition now apply the projection after world/hand
rendering and before block/water/fire/portal/HUD overlays; Nausea suppresses
the full-screen portal texture. Native checkpoint reload retains current,
previous, and pending-contact client state. The projection has a focused
mutation test, but stable real-client pixel closure remains open.

2026-08-14 area-effect-cloud common-Entity checkpoint: authoritative capture,
capsule validation, script restore, native output, and ticking now retain
dimension, air/fire/portal counters, ground/gravity/invulnerable/silent/glowing
flags, Forge `UpdateBlocked`, water/first-update state, fall distance,
previous/last-tick position, and the server `Entity.rand` cursor plus Gaussian
cache. A parked real server and native match both a normal world-wrapper tick
and the discriminating blocked tick at raw float/double precision. The native
434-check brewing suite also locks immune-fire decrement, water extinguish,
and water-entry RNG advancement. The independent four-case real-client cloud
particle gate still matches every call and final client RNG cursor, proving
the server cursor did not contaminate rendering randomness. Generic nonempty
name/tag/command-stat/Forge-data/capability/passenger state and active portal
entry fail closed rather than entering the exact subset.

2026-08-14 Saturation/Luck checkpoint: a locked real-player oracle matches the
active Saturation III `FoodStats` transition, one-tick expiry, Luck II plus
Unluck I attribute result, and the vanilla no-op custom-drink Saturation path.
The live fishing reel adds that player attribute to Luck of the Sea. The real
1.11.2 nested fishing table agrees across 48 seeded results spanning luck -2
through 4, including damage and enchantment NBT. The broad native player-effect
and runtime suites pass.

2026-08-08 all-living target-audio checkpoint: the locked real-Java attack
fixture passes 115 cases. Thirty-six new rows cover hurt/death audio for
zombies, pigmen, skeletons, wither skeletons, creepers, spiders, cave spiders,
endermen, blazes, ghasts, silverfish, villagers, and slime/magma sizes 1, 2,
and 4. Identity, target position, HOSTILE versus NEUTRAL category, exact
private-RNG pitch bits, ghast volume 10.0, size-scaled slime/magma volume,
small/big event selection, and pigman pre-damage anger RNG all match Java.
The focused native runtime passes 50 cases and OpenAL validates all 176 events
and 557 owned variants. The full native aggregate passes in 5:31.17 with
450,612 KiB peak RSS, zero major faults, and zero swap. The stopped-oracle CPU
guard passes at 4,635 scalar steps/s against the frozen 3,858.9 floor and the
4,062 baseline. The product and Java builds pass. All 13 locally available
quick-sweep stages pass; the two snapshot-backed Blaze stages skip because
their `.bsnp` inputs are absent. GPU 1 was not executed.

2026-08-08 lingering-cloud particle checkpoint: a parked real-Java client
branch matches native for all 29 default active-cloud `SPELL_MOB` calls, the
waiting branch's Boolean gate and two calls, every spawn argument bit, and the
final 48-bit entity RNG cursor. The fixed 1,024-event runtime stream feeds a
deterministic vanilla-formula ParticleSpell renderer; unsaved Java constructor
entropy remains outside the pixel claim. Focused Java/native, particle,
capsule, brewing, and product gates pass. The uninterrupted full native
aggregate passes in 7:17.20 at 1,100,916 KiB peak RSS. The stopped-oracle CPU
guard passes at 4,873 scalar steps/s against the frozen 3,858.9 floor and 4,062
baseline. All 13 locally available quick-sweep stages pass; the two
snapshot-backed Blaze stages skip because their `.bsnp` inputs are absent. GPU
1 was not executed.

2026-08-08 brewing-automation checkpoint: six real-Java/native rows match the
brewing stand's top, side, and bottom slot exposure and validation. They cover
ingredient, potion, and blaze-powder insertion, bottom potion extraction,
ordinary ingredient rejection, the glass-bottle exception, exact hopper
cooldown 8, and same-tick fuel 20. The expanded automation oracle and Java
build pass. No product-path code, allocation, or idle work was added.

2026-08-08 multi-slot automation checkpoint: real Java and native match two
seven-callback, three-occupied-slot fixtures for dropper insertion and default
dispenser ejection. Every selected slot and the final shared 48-bit
`TileEntityDispenser.RNG` cursor are exact, including the first `nextInt(1)`
draw. The cursor is injectible and visible in raw state. Automatic transport of
Java's unsaved process-global cursor remains open; the runtime-local default is
deterministic and adds no allocation or idle-world work.
The focused automation and brewing gates pass. The full native aggregate
passes in 6:57.96 at 1,100,896 KiB peak RSS; CPU throughput passes at 4,126
steps/s against the 3,858.9 floor and 4,062 baseline. All 13 locally available
quick-sweep stages pass, with only the two absent-snapshot Blaze stages
skipping. GPU 1 was not executed.

2026-08-08 empty-bucket automation checkpoint: five real-Java/native rows
match singleton water/lava source pickup, non-fluid default ejection, stacked
pickup into the first empty slot, and full-dispenser filled-bucket ejection
through Forge's production `DispenseFluidContainer`. Inventory/block mutation,
emitted item, final world RNG/Gaussian cursor, and both nested/outer
`1000/2000` event pairs are exact. The focused thirteen-behavior automation
oracle passes; no idle work or allocation was added.

2026-08-08 furnace/double-chest automation checkpoint: ten real-Java/native
rows match furnace top input and side fuel insertion, bottom output and
fuel-slot water-bucket extraction, ordinary bottom-fuel rejection, and
canonical west-first insertion/continuation/extraction across a 54-slot double
chest. Two additional rows prove hopper lookup ignores a solid block above the
addressed or adjacent half, unlike player/comparator access. Every inventory
count and successful hopper cooldown 8 matches. This promotes an existing
fixed-capacity path and adds no idle work or allocation.

2026-08-08 trapped-chest/shulker automation checkpoint: five real-Java/native
rows match trapped double-chest west-first insertion/extraction, ordinary item
insertion into and extraction from a shulker box, nested-shulker rejection, and
successful cooldown 8. The focused automation oracle passes; all operations
reuse fixed-size inventories and add no idle scan or allocation. The full
native aggregate passes in 7:16.75 at 1,101,120 KiB peak RSS with zero major
faults and zero swap. CPU throughput passes at 4,844 steps/s against the
3,858.9 floor and 4,062 baseline. All 13 locally available quick stages pass;
the two absent-snapshot Blaze stages skip. GPU 1 was not executed.

2026-08-08 default-ejection automation checkpoint: six real-Java/native rows
match ordinary block metadata, tool damage, non-white dye, milk bucket, and
ejection through a solid-facing target. Source inventory, emitted payload,
unchanged target, final world RNG/Gaussian state, and both world events are
exact. Five native negative controls keep registered but unimplemented
shulker, armor, minecart, shield, and elytra behaviors out of the default path.
The focused automation oracle passes; no allocation or idle work was added.

2026-08-08 minecart/shulker dispenser checkpoint: nine minecart rows match all
six item kinds, flat/ascending/lower rail geometry, same-boundary cart motion,
and blocked-target nested default ejection. Eighteen shulker rows match all 16
colors, air/support facing, blocked failure, source item, target block/meta,
empty tile creation, and ordered events. The oracle also promoted exact
inventory-dependent chest/hopper-cart drag; the independent minecart rail gate
passes. No idle-world work was added.
The uninterrupted native aggregate passes in 7:17.41 at 1,100,908 KiB peak
RSS with zero major faults and zero swap. CPU throughput passes at 4,913
steps/s against the 3,858.9 floor and 4,062 baseline. All 13 locally available
quick stages pass; the two absent-snapshot Blaze stages skip. GPU 1 was not
executed.

2026-08-08 boat-variant dispenser checkpoint: eight real-Java/native rows
match all six `EntityBoat.Type` ordinals, direct-water and air-over-water spawn
pose, source item, blocked-target emitted item, RNG/Gaussian state, and ordered
nested/outer events. The broader native mob/boat battery passes. Non-oak
textures and automatic type transport remain explicitly open; no idle work was
added.

2026-08-08 equipment dispenser checkpoint: 50 real-Java/native rows match all
armor items, shield, and elytra at an empty target and represented player,
including equipment-slot selection, item damage, source mutation,
RNG/Gaussian state, events, Java's transient empty-item success bug, and six
occupied-slot default-ejection/immediate-pickup cases. A native mob-target
control remains fail-closed until mob equipment/order is represented. The scan
is bounded and firing-only; no idle work was added. The uninterrupted aggregate
gameplay gate passes in 7:11.57
at 1,101,168 KiB maximum RSS, zero major faults, and no swap. The isolated CPU
guard passes at 4,933 steps/s against the 3,858.9 floor and 4,062 baseline. All
13 locally available quick-sweep stages pass; the two absent-snapshot Blaze
stages skip. GPU 1 was not executed.

2026-08-08 skull/pumpkin dispenser checkpoint: 13 real-Java/native rows match
all six skull metadata values and pumpkins for represented-player equip,
occupied-head failure, and empty non-pattern failure. The gate includes exact
source/equipment state, unchanged RNG/Gaussian cursor, and optional
`1001/2000` and `1000/2000` events, including Java's singleton
failure-sound-on-success edge and stacked equip's normal success event.
Possible wither/snow-golem/iron-golem patterns and represented mob targets fail
closed.
The neighborhood scan is bounded and firing-only; no idle work was added.
The uninterrupted aggregate gameplay gate passes in 7:09.29 at 1,100,292 KiB
maximum RSS, zero major faults, and no swap. The CPU guard passes at 4,139
steps/s against the 3,858.9 floor and 4,062 baseline. All 13 locally available
quick-sweep stages pass; the two absent-snapshot Blaze stages skip. GPU 1 was
not executed.

2026-08-08 player chest-obstruction checkpoint: 12 real-Java/native rows match
ordinary and trapped single/double chest access under air, stone, lower and
upper stone slabs, and stone over either double-chest half. The exact lower-face
`isSideSolid` distinction blocks a lower slab and admits an upper slab. Existing
covered-double-chest rows still prove hopper insertion ignores the player-only
rule. The bounded player-use query adds no allocation or idle work; sitting
ocelots remain open with that entity family.
The corrected uninterrupted aggregate gate passes in 7:20.59 at 1,100,924 KiB
maximum RSS, zero major faults, and no swap. The CPU guard passes at 4,632
steps/s against the 3,858.9 floor and 4,062 baseline. All 13 locally available
quick-sweep stages pass; the two absent-snapshot Blaze stages skip. GPU 1 was
not executed.

2026-08-08 bounded-plant bonemeal dispenser checkpoint: 67 locked
real-Java/native rows match wheat, carrots, potatoes, beetroot, pumpkin/melon
stems, cocoa, tall grass, ferns, and both halves of all four clone-producing
double plants, all six stage-zero sapling types, brown/red mushrooms, and
flat/plains grass spreading. The
gate covers initial, near-mature, and mature ages under two exact world-RNG
branches, three supported cocoa facings, two-block grass/fern promotion,
dead-bush failure, both outcomes of saplings' exact `nextFloat < 0.45` branch,
and exact sunflower/lilac/rose/peony clone item construction plus its same-tick
update. Mushroom rows cover `nextFloat < 0.4` rejection, ordinary and doubled
height, obstruction rollback, and every state in a 735-cell raw structure
cuboid with the fixture dispenser cell normalized out. Source item/count/meta,
surrounding
block state, item pose/motion/yaw/hover/state, RNG/EID cursors, and ordered
`2005/1000/1001/2000` events match, including beetroot's successful consume
with zero growth and cocoa's no-RNG single-stage growth. Three grass rows
compare all 450 platform/plant-layer states and lock the exact 128-attempt
random walk, normal-cube/support checks, tall-grass placements, plains
Forge-weighted flowers, and final world-RNG cursor. Non-plains flower weighting
remains outside this flat-world promotion.
The repaired isolated-oracle launcher now copies each generated config into its
actual run directory and passes the unique qrl port and username through the
documented Gradle properties. The uninterrupted native aggregate passes in
7:43.25 at 1,101,168 KiB maximum RSS, zero major faults, and no swap. The CPU
guard passes at 4,391 steps/s against the 3,858.9 floor and 4,062 baseline. All
13 locally available quick-sweep stages pass; the two absent-snapshot Blaze
stages skip. GPU 1 was not executed.

2026-08-08 stage-one sapling dispenser checkpoint: the locked plant gate now
passes 78 real-Java/native rows. Eleven new rows compare every state in a
34,596-cell raw volume for ordinary oak, spruce, birch, jungle, and acacia;
fresh-generator big oak; single dark-oak failure; required 2x2 dark oak; exact
dispenser-obstructed 2x2 spruce/jungle rollback; and ordinary-oak obstruction
rollback. Source inventory, the complete world-RNG stream, ordered events,
logs/leaves/soil, and legacy metadata all match. The metadata gate includes
Java's notified write-order effect where replacing a leaf with a log marks the
currently adjacent 3x3x3 leaves `CHECK_DECAY`; this also locks big-oak limbs and
dark-oak hanging branches. Independent FNV constants captured from Java promote
all eleven volumes into the offline automation aggregate. The live generator
uses fixed scratch allocated only on the first firing, bulk block writes, one
relight/version finish, and no idle-tick scan. The focused aggregate completes
in 9.44 seconds at 172,760 KiB peak RSS with zero major faults.

2026-08-08 all-biome grass-bonemeal checkpoint: the locked plant gate now
passes 89 real-Java/native rows. Eleven new grass rows cover the default forest
table, plains mutation, swamp and its mutation, and flower forest across three
RNG seeds where applicable. Every one of the 450 platform/plant-layer states,
the selected biome, source inventory, events, and final RNG cursor match. All
14 grass volumes also have independent Java-derived FNV constants in the fast
native automation battery, which passes at 172,156 KiB peak RSS with zero major
faults and no swap. The test changes only a temporary per-column fixture hook;
the firing-time production implementation and idle-tick cost are unchanged.
The final native aggregate passes in 8:00.76 at 1,100,524 KiB peak RSS with
zero major faults and no swap. The CPU guard passes at 4,474 steps/s against
the 3,858.9 floor and 4,062 baseline. All 13 locally available quick-sweep
stages pass; the two absent-snapshot Blaze stages skip.

2026-08-08 player-applied sapling bonemeal checkpoint: the locked plant gate
now passes 112 real-Java/native rows. Twenty-three new player rows cover both
stage-zero RNG outcomes for all six sapling types and all eleven stage-one
dispatch cases, including successful unobstructed 2x2 spruce, jungle, and dark
oak, single dark-oak failure, big oak, and obstruction rollback. The production
main-hand path uses the real reach raycast, consumes one white dye outside
creative mode, emits event 2005, and swings only on success. Plant growth is
shared with the dispenser front end, so the complete raw result volume and
world-RNG cursor remain identical at the call boundary. The live comparison
passes in 16.41 seconds at 30,252 KiB peak RSS. The broad offline automation
battery passes in 17.88 seconds at 171,752 KiB peak RSS, zero major faults,
and no swap. The full native aggregate passes in 7:47.51 at 1,101,000 KiB
peak RSS, zero major faults, and zero swap. The CPU guard passes at 4,446
steps/s against the 3,858.9 floor and 4,062 baseline. All 13 locally available
quick-sweep stages pass; the two absent-snapshot Blaze stages skip. No
idle-tick scan or allocation was added.

2026-08-08 natural sapling random-tick checkpoint: the locked plant gate now
passes 136 real-Java/native rows. Twenty-four new natural rows cover both
`nextInt(7)` outcomes for all six stage-zero species, low-light rejection, and
all eleven stage-one ordinary/big/2x2 generator cases with exact raw volumes,
world hashes, RNG cursors, and zero dispenser/player side effects. The live
comparison passes in 17.31 seconds at 30,252 KiB peak RSS with zero major
faults. A separate strict `sapling_random_selection_seed_0` matrix case drives
the real isolated `WorldServer` selector and matches the selected cell,
`updateLCG`, stage mutation, final world RNG, and raw state in 18.2 seconds.
The standalone tracer manifest was synchronized with the main game's particle,
audio, and window-composition objects so this matrix lane builds again. The
broad automation battery passes in 16.92 seconds at 172,728 KiB peak RSS with
zero major faults. The full native aggregate passes in 7:45.53 at 1,101,160
KiB peak RSS. CPU throughput passes at 4,553 steps/s against the 3,858.9 floor
and 4,062 baseline. All 13 locally available quick-sweep stages pass in 54.66
seconds; the two absent-snapshot Blaze stages skip.

2026-08-08 offhand player-bonemeal checkpoint: the locked plant gate passes
159 real-Java/native rows. Twenty-three new offhand rows mirror every sapling
stage-zero RNG outcome and stage-one ordinary/big/2x2 generator case with exact
stack mutation, events, raw volumes, world hashes, and RNG cursors. The
playable empty-main route additionally raycasts and consumes offhand white dye,
emitting one 2005 event and swinging only on success. The live gate passes in
24.03 seconds at 30,252 KiB peak RSS with zero major faults. The broad
automation battery, including direct main/offhand and both playable hand
routes, passes in 16.75 seconds at 172,184 KiB peak RSS with zero major faults.
The full native aggregate passes in 7:42.46 at 1,100,540 KiB peak RSS with zero
major faults. CPU throughput passes at 4,624 steps/s against the 3,858.9 floor
and 4,062 baseline. All 13 locally available quick-sweep stages pass in 54.97
seconds; the two absent-snapshot Blaze stages skip.

2026-08-08 crop-family natural-random-tick checkpoint: the locked plant gate
passes 222 real-Java/native rows. Sixty-three new rows cover wheat, carrots,
potatoes, beetroot, pumpkin/melon stems, and cocoa on canonical valid support.
They lock growth acceptance/rejection/maturity, beetroot's independent
`nextInt(3)` throttle, all cocoa facings and age/RNG exits, both young stems,
all four mature fruit directions, existing-adjacent-fruit early exit, selected
target obstruction, pumpkin metadata 2, melon metadata 0, exact result volumes,
and final world-RNG cursors. The live gate passes in 21.62 seconds at 30,252
KiB peak RSS with zero major faults. The broad automation regression passes in
17.56 seconds at 172,204 KiB peak RSS with zero major faults. The full native
aggregate passes in 7:46.58 at 1,101,100 KiB peak RSS. CPU throughput passes at
4,657 steps/s against the 3,858.9 floor and 4,062 baseline. All 13 locally
available quick-sweep stages pass in 54.96 seconds; the two absent-snapshot
Blaze stages skip.

2026-08-08 plant-column natural-random-tick checkpoint: the locked plant gate
passes 239 real-Java/native rows. Seventeen new rows cover nether wart, cactus,
and sugar cane on canonical support. They lock exact wart growth rejection,
acceptance, maturity, and `nextInt(10)` cursors; deterministic column age 0,
14, and 15 behavior; one-, two-, and three-block heights; source reset and
default-state extension; blocked ceilings; and unchanged RNG for cactus/cane.
The live gate passes in 21.74 seconds at 30,252 KiB peak RSS with zero major
faults. Broad automation passes in 17.18 seconds at 172,740 KiB peak RSS with
zero major faults. The uninterrupted native aggregate passes in 7:36.70 at
1,101,160 KiB peak RSS, three major faults, and zero swap. CPU throughput
passes at 4,816 steps/s against the 3,858.9 floor and 4,062 baseline. All 13
locally available quick-sweep stages pass in 52.76 seconds; the two
absent-snapshot Blaze stages skip. GPU 1 was not executed.

2026-08-08 controlled-vine-random-tick checkpoint: the locked callback gate
passes 369 real-Java/native rows. Seventeen new rows cover the no-roll cursor,
all six direction classes, upward/downward metadata filtering, both rotated
lateral attachments, diagonal and ceiling fallbacks, three solid-face
acquisitions, lower-vine merging, the exact five-vine density cutoff, and the
density-independent downward path. The comparator locks all 243 local states
and the World RNG cursor. The live gate passes in 36.30 seconds at 30,252 KiB
peak RSS with zero major faults and zero swap. The native runtime passes in
5:10.16 at 257,560 KiB peak RSS with zero major faults and zero swap. CPU
throughput passes at 4,951 steps/s against the 3,858.9 floor and 4,062 baseline.
All 13 locally available quick-sweep stages pass in 52.78 seconds at 716,748
KiB peak RSS; the two absent-snapshot Blaze stages skip. GPU 1 was not
executed.

2026-08-08 vine-neighbor-support checkpoint: a dedicated locked gate passes
8/8 real-Java/native rows in 1.26 seconds at 30,252 KiB peak RSS. It compares
800 raw block states across total break, partial face pruning, inherited-face
survival, mismatched inheritance, mixed direct/inherited support,
metadata-zero ceiling removal, and two- and three-vine synchronous cascades.
World RNG, Math RNG, and the entity-ID cursor remain exact and unchanged. The
broader plant gate remains 369/369 in 36.85 seconds. The native runtime battery
passes in 5:43.71 at 471,812 KiB peak RSS with zero major faults and zero swap.
CPU throughput passes at 4,905 steps/s against the 3,858.9 floor and 4,062
baseline. All 13 locally available quick-sweep stages pass in 56.62 seconds at
716,824 KiB peak RSS; the two absent-snapshot Blaze stages skip. GPU 1 was not
executed.

2026-08-08 natural-mushroom-spread checkpoint: the locked plant gate passes
247 real-Java/native rows. Eight new rows cover both mushroom colors on a
canonical mycelium platform, including two outer-roll rejection cursors, the
accepted fixed 21-draw walk and exact three-block-east placement, and the
five-mushroom density early exit after only the outer draw. Every state in the
507-cell platform/result volume and the final world-RNG cursor match. The live
gate passes in 25.77 seconds at 30,252 KiB peak RSS with zero major faults.
Broad automation passes in 17.17 seconds at 172,752 KiB peak RSS with zero
major faults. The uninterrupted native aggregate passes in 7:33.78 at
1,100,904 KiB peak RSS, zero major faults, and zero swap. CPU throughput passes
at 4,784 steps/s against the 3,858.9 floor and 4,062 baseline. All 13 locally
available quick-sweep stages pass in 54.23 seconds; the two absent-snapshot
Blaze stages skip. GPU 1 was not executed.

2026-08-08 controlled-grass-random-tick checkpoint: the locked plant gate
passes 251 real-Java/native rows. Four new rows cover two exact bright-spread
cells and their twelve-draw cursors, a twelve-draw no-placement path, and dark
stone-cap decay to default dirt with no RNG. Every state in the 450-cell
platform/result volume matches. The live gate passes in 28.37 seconds at
30,252 KiB peak RSS with zero major faults. Broad automation passes in 17.24
seconds at 171,848 KiB peak RSS with zero major faults. The uninterrupted
native aggregate passes in 7:10.07 at 1,101,180 KiB peak RSS, zero major
faults, and zero swap. CPU throughput passes at 4,776 steps/s against the
3,858.9 floor and 4,062 baseline. All 13 locally available quick-sweep stages
pass in 54.51 seconds; the two absent-snapshot Blaze stages skip. GPU 1 was not
executed.

2026-08-08 controlled-mycelium-random-tick checkpoint: the locked plant gate
passes 255 real-Java/native rows. Four new rows cover two exact bright-spread
cells and their twelve-draw cursors, a twelve-draw no-placement path, and dark
stone-cap decay to default dirt with no RNG. Every state in the 450-cell
platform/result volume matches. The live gate passes in 30.79 seconds at
30,252 KiB peak RSS with zero major faults. Broad automation passes in 16.70
seconds at 172,748 KiB peak RSS with zero major faults. The uninterrupted
native aggregate passes in 7:06.80 at 1,100,148 KiB peak RSS, zero major
faults, and zero swap. CPU throughput passes at 4,984 steps/s against the
3,858.9 floor and 4,062 baseline. All 13 locally available quick-sweep stages
pass in 52.15 seconds; the two absent-snapshot Blaze stages skip. GPU 1 was not
executed.

2026-08-08 controlled-farmland-random-tick checkpoint: the locked plant gate
passes 262 real-Java/native rows. Seven new rows cover moisture 7-to-6 and
1-to-0 dry-down, uncovered moisture-zero dirt conversion, wheat-covered
preservation, static-water hydration from both moisture 3 and 7, and open-sky
rain hydration. Every state in the exact 9x2x9 water-search/result volume and
the unchanged world-RNG cursor match. The live gate passes in 32.17 seconds at
30,252 KiB peak RSS with zero major faults. Broad automation passes in 17.45
seconds at 172,196 KiB peak RSS with zero major faults. The uninterrupted
native aggregate passes in 7:46.86 at 1,101,160 KiB peak RSS, zero major
faults, and zero swap. CPU throughput passes at 4,307 steps/s against the
3,858.9 floor and 4,062 baseline while unrelated training jobs use the host.
All 13 locally available quick-sweep stages pass in 56.86 seconds; the two
absent-snapshot Blaze stages skip. GPU 1 was not executed.

2026-08-08 controlled-snow-layer-random-tick checkpoint: the locked plant gate
passes 278 real-Java/native rows. Sixteen new rows cover every layer metadata
in dark and glowstone-lit fixtures. The exact target block-light value,
retained metadata or air result, and unchanged world-RNG cursor match. The
live gate passes in 32.74 seconds at 30,252 KiB peak RSS with zero major
faults. Broad automation passes in 17.10 seconds at 170,972 KiB peak RSS with
zero major faults. The uninterrupted native aggregate passes in 7:05.58 at
1,100,952 KiB peak RSS, zero major faults, and zero swap. CPU throughput passes
at 4,515 steps/s against the 3,858.9 floor and 4,062 baseline. All 13 locally
available quick-sweep stages pass in 53.56 seconds; the two absent-snapshot
Blaze stages skip. GPU 1 was not executed.

2026-08-08 controlled-ice-random-tick checkpoint: the locked plant gate passes
280 real-Java/native rows. Two new rows prove the exact block-light threshold:
dark ice remains 79:0, while block light nine produces flowing water 8:0 and
one scheduled `[0,0,0,8,5,0,0]` entry. Block/meta, measured light, relative
position, due delay, priority, queue rank, events, and all RNG cursors match.
The fixture now asserts an empty local scheduled queue at every case boundary
and removes case-owned work during teardown, closing a leaked falling-sand
callback from earlier cactus setup. The live gate passes in 32.53 seconds at
30,252 KiB peak RSS with zero major faults. Broad automation passes in 33.85
seconds at 172,056 KiB peak RSS with zero major faults while unrelated CPU
experiments are active. The uninterrupted native aggregate passes in 7:27.23
at 1,100,880 KiB peak RSS, zero major faults, and zero swap. CPU throughput
passes at 4,472 steps/s against the 3,858.9 floor and 4,062 baseline. All 13
locally available quick-sweep stages pass in 54.45 seconds; the two
absent-snapshot Blaze stages skip. GPU 1 was not executed.

2026-08-08 controlled-full-snow-block-random-tick checkpoint: the locked plant
gate passes 282 real-Java/native rows. Two new rows prove the exact stored
block-light threshold: light 11 retains snow block 80:0 with no cursor change;
light 12 produces air plus four separate snowball 332:0 entities. Block state,
measured callback light, every item position and velocity, yaw and hover float
bits, stack/lifecycle fields, world and Math RNG cursors, causal entity IDs,
and events match. The fixture uses the same direct saved-light nibble write as
Java `World.setLightFor(BLOCK)`, so this does not weaken or bypass native
relighting. The live gate passes in 32.07 seconds at 30,252 KiB peak RSS with
zero major faults. Broad automation passes in 34.43 seconds at 172,740 KiB
peak RSS, zero major faults, and zero swaps. The uninterrupted native aggregate
passes in 9:07.07 at 1,101,172 KiB peak RSS, zero major faults, and zero swaps
while unrelated CPU experiments are active. CPU throughput passes at 4,019
steps/s against the 3,858.9 floor and 4,062 baseline. All 13 locally available
quick-sweep stages pass in 54.20 seconds at 715,400 KiB peak RSS; the two
absent-snapshot Blaze stages skip. GPU 1 was not executed.

2026-08-08 controlled-frosted-ice-random-tick checkpoint: the locked plant
gate passes 292 real-Java/native rows. Eight age/light rows cover metadata zero
through three on both retain and melt branches. A sparse five-neighbor star
proves Java's old-block notification collapse and exact target/WEST/EAST/UP/
NORTH/SOUTH flowing-water queue order. A supported dense ring proves that the
neighbors survive notification, then age in the separate six-face propagation
pass with exact 20-through-40-tick callbacks. The comparator locks combined
light, all 11 local states, relative scheduled positions, block, delay,
priority, rank, events, and RNG cursors. The live gate passes in 33.23 seconds
at 30,252 KiB peak RSS with zero major faults and zero swap. Broad automation
passes in 39.79 seconds at 172,032 KiB peak RSS with one major fault and zero
swap. The uninterrupted native aggregate passes in 8:11.20 at 1,100,992 KiB
peak RSS, zero major faults, and zero swap. CPU throughput and the quick sweep
pass at 4,722 steps/s against the 3,858.9 floor and 4,062 baseline. All 13
locally available quick-sweep stages pass in 59.37 seconds at 716,824 KiB peak
RSS; the two absent-snapshot Blaze stages skip. GPU 1 was not executed.

2026-08-08 controlled-chorus-flower-random-tick checkpoint: the locked plant
gate passes 332 real-Java/native rows. Forty new rows cover all six ages on
direct end stone, obstruction-driven zero/two-branch lateral outcomes,
two-deep unconditional vertical growth, and rooted three-deep depth acceptance
or rejection. Seed 2 forces exact south-then-north lateral growth at ages zero
and three and terminal death at age four. The comparator locks all 150 local
states, World RNG cursor, and ordered 1033/1034 grow/death events. The live
gate passes in 34.42 seconds at 30,252 KiB peak RSS with zero major faults and
zero swap. The final scheduler-integrated native runtime passes in 4:47.74 at
257,036 KiB peak RSS with zero major faults and zero swap. CPU throughput
passes at 4,663 steps/s against the 3,858.9 floor and 4,062 baseline. All 13
locally available quick-sweep stages pass in 55.48 seconds at 716,788 KiB
peak RSS; the two absent-snapshot Blaze stages skip. GPU 1 was not executed.

2026-08-08 controlled-static-lava-random-tick checkpoint: the locked callback
gate passes 352 real-Java/native rows. Eight original rows cover upward
ignition for both positive outer values, two independent zero-branch floor
patterns with three fires each, first-solid abort, and `doFireTick=false` for
both outer classes. Twelve expansion rows cover Java's wood, leaves, cloth,
TNT, vine, and carpet burning materials, including the non-moving vine/carpet
continuation branch. The comparator locks all 245 local states, World RNG
cursor, exact fire positions, successor delay, priority, and queue rank. The
live gate passes in 36.26 seconds at 30,252 KiB peak RSS with zero major faults
and zero swap. The full native runtime battery passes in 5:57.02 at 469,864 KiB
peak RSS with zero major faults and zero swap. CPU throughput passes at 4,666
steps/s against the 3,858.9 floor and 4,062 baseline. All 13 locally available
quick-sweep stages pass in 54.40 seconds at 716,808 KiB peak RSS; the two
absent-snapshot Blaze stages skip. GPU 1 was not executed.

2026-08-08 leaf-decay-and-break-lifecycle checkpoint: a dedicated locked gate
passes 13/13 real-Java/native rows in 22.15 seconds at 30,252 KiB peak RSS,
with zero major faults and zero swap. The comparator locks an 11x11x11 volume
per row, 17,303 raw states in total, every spawned item field, and World,
Math, Block, and entity-ID cursors. It covers decay-disabled and nondecayable
controls, distance-four support through mixed leaf families, distance-five
and diagonal-only rejection, exact oak/jungle/acacia/dark-oak drop branches,
decay-driven vine support loss, log-removal marking across the surrounding
9-cube, and leaf-removal marking across the surrounding 3-cube. The broader
plant/random-tick gate remains 369/369 in 36.74 seconds. The final native
runtime passes in 5:08.58 at 256,056 KiB peak RSS with zero major faults and
zero swap. The Java build passes. CPU throughput passes at 4,727 steps/s
against the 3,858.9 floor and 4,062 baseline. All 13 locally available quick-
sweep stages pass in 1:30.55 at 715,388 KiB peak RSS; the two absent-snapshot
Blaze stages skip. GPU 1 was not executed.

2026-08-08 mixed-opacity-sky-column checkpoint: a dedicated parked gate passes
13/13 real-Java/native rows in 0.52 seconds at 30,252 KiB peak RSS, with zero
major faults and zero swap. It compares 75,504 block/skylight scalar values,
exact pre/post delayed queues, and World, Math, Block, and entity-ID cursors.
The rows cover stone, leaves, water, and glass additions/removals, sequential
stacked water, opaque/water overhangs, and local-x15 stone/water edits. Exact
transition tables pin the affected vertical cells and the static-to-flowing
water side effect plus priority-zero +5 schedule. A sub-second native piston-
waterlily regression additionally locks cold static-water neighbor wakeup. The
Java build passes. The full native aggregate passes in 6:02.64 at 478,256 KiB
peak RSS with zero major faults and zero swap. CPU throughput passes at 4,874
steps/s against the 3,858.9 floor and 4,062 baseline. All 13 locally available
quick-sweep stages pass in 55.01 seconds at 716,876 KiB peak RSS; the two
absent-snapshot Blaze stages skip. GPU 1 was not executed.

2026-08-08 static-lava-neighbor checkpoint: a dedicated parked gate passes
58/58 real-Java/native rows in 3.59 seconds at 46,160 KiB peak RSS, with zero
major faults and zero swap. It compares 14,500 raw block states, exact pre/post
queues, sound coordinates/volume/pitch bits, 48 double-valued smoke payload
components per mixing row, and World, Math, Block, and entity-ID cursors. The
rows lock nonmixing conversion at levels 0, 4, 5, and 15, the +30
Overworld/End and +10 Nether callbacks, DOWN exclusion, static/flowing water
material checks, source
obsidian, levels-one-through-four cobblestone, higher-level fallback, and
nested +5 static-water wakeups. Twenty-four direct dynamic rows additionally
lock Nether/End decay one/two, cadence 10/30, queue order, and a shaped-floor
discriminator for Nether's four-step versus End's two-step slope search. They
also lock the exact `nextInt(4)` slowdown and +40/+120 schedules, plus Forge's
default refusal to create lava sources from two neighbors. Nether/End downward
flow into air matches falling level eight and exact child/source queue order.
Horizontal flow replaces fire and static/flowing water in both dimensions with
exact queues, lava-extinguish effects, and final RNG cursors. The water rows
also lock source-to-obsidian reentrancy, the two ordered effect streams, and
static water's stale +5 queue. Downward water contact forms stone and now
matches its effect stream and source requeue. Exact scheduled-fluid ownership
retires only an overlapping approximate-CA region, preventing a second engine
from rewriting the same cells while leaving remote live flows active.
Six Nether rows additionally replace valid sapling, web, floor torch, snow
layer, wall vine, and carpet states with exact lava/effects/queues. A supported
ladder control remains unchanged with no schedule or RNG use, locking Java's
explicit `isBlocked` exception to the generic material predicate. Nine more
rows cover ordinary/powered rail, redstone wire, tall grass, wheat, dead bush,
brown mushroom, lit redstone torch, and supported reeds. They lock powered
rail's center-before-child queue order, sand's unconditional +2 callback, and
the reed liquid exception. Ten common-plant rows cover both flowers, red
mushroom, pumpkin/melon stems, waterlily, nether wart, carrots, potatoes, and
beetroot. Waterlily also locks support water's +5 queue before child/source
lava.
The playable sound manifest and deterministic
vanilla-formula `ParticleSmokeLarge` renderer are wired and their focused tests
pass. The fluid gate passes in 3.70 seconds at 41,768 KiB peak RSS. The full
native runtime battery passes in 7:41.70 at 1,101,108 KiB peak RSS with zero
major faults and zero swap; it caught and corrected a stale scheduled-tick
fixture boundary before the clean pass. The Java build passes. CPU throughput
passes at 4,730 steps/s against the 3,858.9 floor and 4,062 baseline. All 13
locally available quick-sweep stages pass in 57.77 seconds at 714,032 KiB peak
RSS with zero major faults and zero swap; the two absent-snapshot Blaze stages
skip. GPU 1 was not executed.

2026-08-08 plant-and-attachment-support checkpoint: a dedicated parked gate
passes 65/65 real-Java/native rows in 12.64 seconds at 30,252 KiB peak RSS,
with zero major faults and zero swap. It compares 4,680 pre/post raw block
states, complete EntityItem identity, metadata, position, motion, pickup delay
and age, plus World, Math, Block, and entity-ID cursors. Nineteen rows cover
ordinary plant families, 36 exhaustive rows cover all snow layers, carpet
colors, and canonical cocoa states, and ten controls lock the accepted and
rejected supports. The product uses the registered destroy payload followed by
the ordinary synchronous air-notification path; snow removes without an item,
carpet drops its color before removal, and cocoa validates the horizontal
jungle log before removal and one-or-three-stack brown-dye drops. The former
approximate upward scan is gone. A short real-oracle client-drain boundary
prevents asynchronously replicated client items from consuming the next
fixture's process-global entity-ID and Math cursors. The first broad aggregate
also exposed and corrected noncanonical cocoa metadata 12 through 15 being
treated as canonical age-three states. Focused plant, random-tick, automation,
native oracle, and Java build gates pass. The focused runtime passes in
5:40.84 at 477,476 KiB peak RSS with zero major faults and zero swap. The
complete native aggregate passes in 7:45.49 at 1,101,128 KiB peak RSS with zero
major faults and zero swap. CPU throughput passes at 4,733 steps/s against the
3,858.9 floor and 4,062 baseline. All 13 locally available quick-sweep stages
pass in 55.15 seconds at 716,924 KiB peak RSS with four major faults and zero
swap; the two absent-snapshot Blaze stages skip. GPU 1 was not executed.

2026-08-09 farmland player-lift checkpoint: the locked plant/random-tick gate
passes 386/386 real-Java/native rows in 35.14 seconds at 30,252 KiB peak RSS.
The additional row stages the actual parked `EntityPlayerMP` in the farmland
chunk without ticking it and proves exact authoritative relative Y 1.0 plus
unchanged global cursors and surrounding state. The fixture restores chunk
membership and all touched pose/history fields. Direct native coverage locks
motion preservation, independent client prediction, and strict just-above
nonintersection. The dragon arena and unrepresented entity classes remain
open. The Java 8 build passes. The exhaustive native runtime passes in 5:41.77
at 491,608 KiB peak RSS with zero major faults and zero swap. The full product
aggregate passes through fresh spawn and credits in 7:37.70 at 1,100,920 KiB
peak RSS, also with zero major faults and zero swap. CPU throughput passes at
4,970 steps/s against the 3,858.9 floor and 4,062 baseline. All 13 locally
available quick-sweep stages pass in 52.52 seconds at 716,792 KiB peak RSS
with zero swap; the two absent-snapshot Blaze stages skip. GPU 1 was not
executed.

2026-08-08 cocoa-invalid-support-random-tick checkpoint: the focused matrix
passes 1/1 against the real Java callback in 17.87 seconds. All 27 saved-state
features match, the 10,625-cell raw block volume is exact, and mature cocoa
removes before three separate brown-dye item constructions with exact
positions, motion, age, pickup delay, World/Math/Block RNG, and entity IDs. The
pre-fix native callback rejected this command without mutating the pod. The
focused runtime passes in 6:33.87 at 477,688 KiB peak RSS with zero major
faults and zero swap; the 65-row support gate and the adjacent random-tick
battery pass. The complete native aggregate passes in 9:09.90 at 1,101,184
KiB peak RSS with zero major faults and zero swap. CPU throughput passes at
4,067 steps/s against the 3,858.9 floor and 4,062 baseline under concurrent
host load. All 13 locally available quick-sweep stages pass in 56.80 seconds
at 716,892 KiB peak RSS with zero major faults and zero swap; the two absent
snapshot stages skip. GPU 1 was not executed.

2026-08-09 Nether ice-vaporization checkpoint: the locked plant/random-tick
gate passes 373/373 real-Java/native rows in 36.73 seconds at 30,252 KiB peak
RSS. Four new Nether rows cover lit ordinary ice and isolated,
sparse-adjacent, and supported-dense mature frosted ice. They lock air rather
than water, synchronous neighbor behavior, exact local states, empty liquid
queues, events, and RNG cursors. The focused random-tick gate passes in 0.46
seconds at 51,856 KiB. The native runtime passes in 5:54.08 at 485,168 KiB
peak RSS. The full product aggregate passes through the fresh-spawn-to-credits
route in 8:00.87 at 1,100,912 KiB peak RSS with zero major faults and zero
swap. CPU throughput passes at 4,897 steps/s against the 3,858.9 floor and
4,062 baseline. All 13 locally available quick-sweep stages pass in 55.35
seconds; the two absent-snapshot Blaze stages skip. GPU 1 was not executed.

2026-08-09 farmland entity-lift checkpoint: the locked plant/random-tick gate
passes 385/385 real-Java/native rows in 35.42 seconds at 30,252 KiB peak RSS.
Twelve occupant rows prove that dry farmland becoming default dirt lifts live
items, both falling-block representations, living mobs, boats, XP orbs,
projectiles, primed TNT, minecarts, fireworks, fish hooks, standalone end
crystals, and area-effect clouds to relative Y 1.0. Direct native coverage also
locks unchanged motion/timers, synchronized living buffers, and strict
just-above nonintersection. The Java 8 build passes. The exhaustive native
runtime passes in 5:02.39 at 256,716 KiB peak RSS. The full product aggregate
passes through fresh spawn and credits in 7:51.65 at 1,101,204 KiB peak RSS
with zero major faults and zero swap. CPU throughput passes at 4,803 steps/s
against the 3,858.9 floor and 4,062 baseline. All 13 locally available
quick-sweep stages pass in 55.49 seconds at 716,960 KiB peak RSS with zero
swap; the two absent-snapshot Blaze stages skip. GPU 1 was not executed.

2026-08-09 farmland dragon-arena lift checkpoint: the locked plant/random-tick
gate passes 388/388 real-Java/native rows in 34.95 seconds at 30,252 KiB peak
RSS. Two new End rows stage a real untracked `EntityDragon` and
`EntityEnderCrystal` without ticking them and prove exact relative Y 1.0 plus
unchanged block, event, RNG, and entity-ID outputs. Direct native coverage
locks the exact 16-by-8 dragon-parent and 2-by-2 crystal boxes, strict
just-above nonintersection, and unchanged dragon motion, head state, and
target state. This completes dirt-conversion lifting for every represented
active store; entity classes absent from the product remain explicit. The
Java 8 build passes. The exhaustive native runtime passes in 5:42.22 at
492,568 KiB peak RSS with zero major faults and zero swap. The full product
aggregate passes through fresh spawn and credits in 7:45.34 at 1,101,164 KiB
peak RSS, also with zero major faults and zero swap. CPU throughput passes at
4,917 steps/s against the 3,858.9 floor and 4,062 baseline. All 13 locally
available quick-sweep stages pass in 52.56 seconds at 714,060 KiB peak RSS;
the two absent-snapshot Blaze stages skip. The callback-only checks add no
idle scan or allocation. GPU 1 was not executed.

2026-08-09 grass-path callback checkpoint: the plant/attachment callback gate
passes 68/68 exact real-Java/native rows in 13.83 seconds at 30,252 KiB peak
RSS. Three new rows lock `onBlockAdded` below solid and non-solid material plus
an unrelated-neighbor trigger, including raw blocks, all three RNG cursors,
and the entity-ID cursor.
The Java 8 build passes. The exhaustive native runtime passes in 5:44.83 at
494,140 KiB peak RSS with zero major faults and zero swap. The full product
aggregate passes through fresh spawn and credits in 7:39.64 at 1,100,904 KiB
peak RSS, also with zero major faults and zero swap. CPU throughput passes at
4,972 steps/s against the 3,858.9 floor and 4,062 baseline. All 13 locally
available quick-sweep stages pass in 52.49 seconds at 716,812 KiB peak RSS;
the two known absent-snapshot Blaze stages skip. The callback adds no idle scan
or allocation. GPU 1 was not executed.

2026-08-09 frosted-ice neighbor checkpoint: the callback gate passes 71/71
exact real-Java/native rows in 13.81 seconds at 30,252 KiB peak RSS. Three new
rows lock sparse collapse after frost removal, retention with two remaining
frost neighbors, and the source-type negative for stone removal, including
raw blocks, exact pending queue/rank, all RNG cursors, and the entity-ID
cursor. The Java 8 build passes. The exhaustive native runtime passes in
5:21.97 at 493,684 KiB peak RSS with zero major faults and zero swap. The full
product aggregate passes through fresh spawn and credits in 7:25.03 at
1,100,908 KiB peak RSS, also with zero major faults and zero swap. CPU
throughput passes at 4,996 steps/s against the 3,858.9 floor and 4,062
baseline. All 13 locally available quick-sweep stages pass in 52.21 seconds at
716,752 KiB peak RSS; the two known absent-snapshot Blaze stages skip. The
source identity adds no idle scan or allocation. GPU 1 was not executed.

2026-08-09 dry-sponge checkpoint: the callback gate passes 76/76 exact
real-Java/native rows in 15.73 seconds at 30,252 KiB peak RSS. Five new rows
lock dry placement, wet retention, unrelated-neighbor triggering,
adjacent-water triggering, and the dense bounded walk. The dense row compares
all 100 raw cells, 65 removals, 26 flowing and eight static retained-water
cells, wet metadata, all 26 priority-zero +5 queue entries and ranks, the
2001/data-9 event, all RNG cursors, and the entity-ID cursor. The Java 8 build
passes. The exhaustive native runtime passes in 4:35.59 at 255,632 KiB peak
RSS with zero major faults and zero swap. The full product aggregate passes
through fresh spawn and credits in 7:25.99 at 1,101,272 KiB peak RSS, also
with zero major faults and zero swap. CPU throughput passes at 5,008 steps/s
against the 3,858.9 floor and 4,062 baseline. All 13 locally available
quick-sweep stages pass in 52.14 seconds at 716,780 KiB peak RSS with zero
major faults and zero swap; the two known absent-snapshot Blaze stages skip.
The callback uses fixed local storage only when triggered and adds no idle scan
or allocation. GPU 1 was not executed.

2026-08-09 Nether-portal invalidation checkpoint: the callback gate passes
81/81 exact real-Java/native rows in 63.53 seconds at 30,252 KiB peak RSS. Five
new rows lock valid minimum X/Z controls, both side-frame breaks, and an
interior break over the full 125-cell volume, including exact queue, event,
item, RNG, and entity-ID state. Dedicated tests also cover the legal 21-by-21
maximum. The Java 8 build passes. The exhaustive native runtime passes in
5:24.64 at 500,756 KiB peak RSS with zero major faults and zero swap. The full
product aggregate passes through credits in 7:28.61 at 1,101,072 KiB peak RSS,
also with zero major faults and zero swap. CPU throughput passes at 4,969
steps/s against the 3,858.9 floor and 4,062 baseline. All 13 locally available
quick-sweep stages pass in 52.99 seconds at 716,828 KiB peak RSS; the two known
absent-snapshot Blaze stages skip. The callback adds no idle scan or
allocation. GPU 1 was not executed.

2026-08-09 fire/cake/torch/ladder checkpoint: the callback gate passes 89/89
exact real-Java/native rows in 68.56 seconds at 30,252 KiB peak RSS. Eight new
rows lock unsupported/retained fire, unsupported/retained cake, floor and wall
torch drops, and north-drop/east-retained ladders across full raw volumes,
queue, event, exact EntityItem, RNG, and EID state. The Java 8 build passes.
The exhaustive native runtime passes in 5:46.88 at 502,756 KiB peak RSS with
zero major faults and zero swap. The full product aggregate passes through
credits in 7:45.57 at 1,101,144 KiB peak RSS, also with zero major faults and
zero swap. CPU throughput passes at 4,875 steps/s against the 3,858.9 floor
and 4,062 baseline. All 13 locally available quick-sweep stages pass in 53.25
seconds at 716,728 KiB peak RSS; the two known absent-snapshot Blaze stages
skip. These callbacks add no idle scan or allocation. GPU 1 was not executed.

2026-08-09 standing/wall-sign checkpoint: the callback gate passes 93/93 exact
real-Java/native rows in 73.57 seconds at 30,252 KiB peak RSS. Four new rows
lock standing-sign drop/retention and north-wall-drop/east-wall-retention
controls across the full raw volume, queue, event, exact item 323 EntityItem,
RNG, and EID state. The Java 8 build and direct native tests pass. The
exhaustive native runtime passes in 5:23.70 at 502,312 KiB peak RSS with zero
major faults and zero swap. The full product aggregate passes through credits
in 7:31.25 at 1,100,092 KiB peak RSS, also with zero major faults and zero
swap. CPU throughput passes at 4,965 steps/s against the 3,858.9 floor and
4,062 baseline. All 13 locally available quick-sweep stages pass in 53.11
seconds at 716,824 KiB peak RSS with one major fault and zero swap; the two
known absent-snapshot Blaze stages skip. The callbacks add no idle scan or
allocation. GPU 1 was not executed.

2026-08-09 default-banner/empty-flower-pot checkpoint: the callback gate passes
99/99 exact real-Java/native rows in 75.37 seconds at 30,252 KiB peak RSS. Six
new rows lock standing/wall banner drop/retention and empty-pot drop/retention
across the full raw volume, queue, event, exact item 425/390 EntityItems, all
RNG cursors, and EID state. Empty-pot tile retirement and its measured extra
post-drop World RNG float are exact. The Java 8 build and direct native tests
pass. The exhaustive native runtime passes in 5:22.24 at 503,008 KiB peak RSS
with zero major faults and zero swap. The full product aggregate passes through
credits in 7:38.78 at 1,101,156 KiB peak RSS, also with zero major faults and
zero swap. CPU throughput passes at 4,982 steps/s against the 3,858.9 floor and
4,062 baseline. All 13 locally available quick-sweep stages pass in 53.84
seconds at 716,776 KiB peak RSS with eight major faults and zero swap; the two
known absent-snapshot Blaze stages skip. The callbacks add no idle scan or
allocation. GPU 1 was not executed.

2026-08-09 note-block checkpoint: a dedicated 12-row parked comparator passes
exact real-Java/native tile state, rising/stable/falling power edges,
blocked-above behavior, all five material-selected instruments, tuning wrap,
blocked tuning, and left-click playback. Sound identity, category, volume and
pitch bits, NOTE particle descriptors, and World/Math RNG cursors match in
2.57 seconds at 30,252 KiB peak RSS. Note tiles round-trip through the strict
state capsule and both trace backends. The owned live sound manifest contains
182 events and 563 variants, and the bounded NOTE particle gate passes. The
Java 8 build and exhaustive native runtime pass; the latter takes 7:01.99 at
506,256 KiB peak RSS with zero major faults and zero swap. CPU throughput
passes at 4,043 steps/s against the 3,858.9 floor. The quick pyramid passes in
71.33 seconds at 716,848 KiB peak RSS; only the two undistributed Blaze
snapshot stages skip. The full `make -C magma test-game` aggregate passes in
575.93 seconds at 1,100,912 KiB peak RSS with zero major faults and zero swap,
including the fresh-spawn-through-credits route. The idle tick path adds no
note-pool scan or allocation. GPU 1 was not executed. A stable Java/native
pixel scene remains open.

2026-08-09 igloo checkpoint: `magma/trace/test_igloo.py` passes 10 exact
real-Java/shared-CPU structures plus four complete deferred-loot inventories.
`game/test_scattered_live.sh` now covers natural seed-0 placement, chest
metadata, resident sites, and runtime one-time materialization. The shared
`overworld_region` CUDA translation unit compiles for `sm_120` with
`--fmad=false`. Focused timings are 15.27 seconds and 30,252 KiB for the Java
gate, 0.34 seconds and 25,920 KiB for natural generation, and 0.64 seconds and
82,088 KiB for runtime materialization, all with zero major faults and swap.
Village, chest-loot, mob-live, Java 8, and the 40.34-second parity-60 bundle
pass. CPU throughput passes at 4,962 steps/s against the 3,858.9 floor and
4,062 baseline. The quick sweep passes all locally runnable stages in 53.94
seconds at 716,788 KiB peak RSS; its two undistributed Blaze snapshot stages
skip. The full product aggregate passes through fresh spawn and credits in
458.33 seconds at 1,101,000 KiB peak RSS with zero major faults and zero swap.
GPU 1 was not executed.

2026-08-09 igloo-resident fidelity checkpoint: the same real-Java/shared-CPU
oracle now compares both residents' position, motion, health, yaw/pitch, fire,
air, profession, persistence, ground, type, and conversion timer. It passes in
13.41 seconds at 30,252 KiB peak RSS. Runtime materialization preserves those
fields in both bounded stores, uses a distinct zombie-villager type, and keeps
the priest's fixed offers. The entity render gate locks the exact eight-box
zombie-villager model, UVs/winding, and all six profession skins; the atlas
gate verifies 53 sprites byte-identical to the owned jar. The combined natural
and runtime scattered gate passes in 2.58 seconds at 115,200 KiB. The shared
CUDA translation unit compiles for `sm_120 --fmad=false` in 83.33 seconds at
3,185,280 KiB; GPU 1 was not executed. CPU throughput passes at 4,958 steps/s
against the 3,858.9 floor and 4,062 baseline. All 13 locally runnable quick
stages pass in 53.20 seconds at 716,788 KiB; the two undistributed Blaze
snapshot stages skip. The full product aggregate passes through fresh spawn
and credits in 455.85 seconds at 1,100,964 KiB peak RSS with zero major faults
and zero swap. Active conversion behavior and a stable pixel scene remain
open.

2026-08-09 swamp-hut witch runtime checkpoint:
`magma/trace/test_swamp_witch.py` passes six exact parked
real-Java/shared-CPU rows and covers both five-percent left-hand outcomes. It
locks pre-first-tick position/motion/base state, health/dimensions/attributes,
persistence, Fire/Air/ground state, the follow-range Gaussian, raw private RNG
cursor, and Gaussian cache. `game/test_scattered_live.sh` locks natural seed-0
site retention, distinct live type, exact represented materialized state,
renderer routing, and permanent one-time claim behavior. Exact potion combat,
loot/sounds, persistence serialization, and pixels remain open.

The Java comparator passes in 0.16 seconds at 30,252 KiB peak RSS. Java 8,
entity-render, mob-live, and the shared `sm_120` CUDA translation-unit compile
pass; the CUDA compile takes 82.07 seconds at 3,185,280 KiB and GPU 1 is not
executed. CPU throughput passes at 4,921 steps/s against the 3,858.9 floor and
4,062 baseline. All locally runnable quick stages pass in 53.10 seconds at
716,776 KiB; only the two undistributed Blaze snapshot stages skip. The full
product aggregate reaches credits in 476.13 seconds at 1,100,928 KiB peak RSS
with one major fault and zero swap.

2026-08-09 Witch self-potion checkpoint:
`magma/trace/test_witch_self_potion.py` passes ten exact parked
real-Java/shared-CPU rows. It locks all four self-potion choices, ordinary and
rare no-choice paths, four completion effects, held/timer/movement state, and
every direct private-RNG draw. `game/test_witch_self_potion_live` additionally
locks ordinary runtime entry, renderer-view held state, status 15, and
next-tick Instant Health. The combined scattered/runtime gate passes in 3.26
seconds at 115,200 KiB and the mob and brewing aggregates pass. Java 8 passes.
The shared CUDA probe compiles for `sm_120 --fmad=false` in 0.50 seconds at
188,640 KiB without executing GPU 1. CPU throughput passes at 4,682 steps/s
against the 3,858.9 floor and 4,062 baseline. At that checkpoint, ranged
potion combat was still open; it is promoted by the following checkpoint.

2026-08-09 Witch ranged splash-potion checkpoint:
`magma/trace/test_witch_ranged.py` passes nine parked real-Java/shared-CPU
rows and compares exact selection, moving-target distance, active-effect
guards, spawn/aim bits, every direct Witch RNG call, sound bits, and seeded
real-EntityPotion motion/rotation/Gaussian state. The 65-check
`game/test_witch_ranged_live` locks all four potion outcomes, cooldown and
visibility edges, drinking suppression, retained queue, runtime construction,
first flight tick, and HOSTILE throw audio. The combined scattered/runtime
gate passes in 3.31 seconds at 115,200 KiB, mob-live passes in 6.82 seconds at
172,320 KiB, and the 389-check brewing gate passes. Java 8 passes. The shared
CUDA probe compiles for `sm_120 --fmad=false` in 0.55 seconds at 188,640 KiB
without executing GPU 1. CPU throughput passes at 4,752 steps/s against the
3,858.9 floor and 4,062 baseline. Audio-live passes for 182 represented events
and 563 loaded variants. The full native aggregate reaches credits in 7:47.04
at 1,101,244 KiB with zero major faults or swap. The clean quick pyramid passes
in 58.52 seconds at 714,016 KiB with zero major faults or swap; only the two
undistributed `.bsnp` stages skip. Drink/hurt/death audio, held-potion/status
pixels, loot, arbitrary NBT/persistence, and constructor entropy remain open.

2026-08-09 Witch lifecycle-audio and player-loot checkpoint:
`magma/trace/test_witch_self_potion.py` now compares the exact drink sound row,
and the real-Java player-attack oracle passes 117 cases including Witch hurt
and death. The owned manifest contains 186 events and 576 weighted variants.
`magma/trace/test_witch_loot.py` passes 13 parked real-Java/shared-CPU rows
covering all seven table entries, one-to-three rolls, zero-count discard,
Looting zero through three, `doMobLoot=false`, exact EntityItem position,
motion, yaw, hover, age/pickup state, all RNG cursors, and the global entity-ID
cursor. `game/test_witch_loot_live` locks three exact emitted stacks, the held
Looting product route, gamerule gating, retained death state, ordered status
and sound events, and atomic fixed-capacity rejection. The scattered/runtime,
mob-live, player-audio, audio-live, Java 8, and shared `sm_120 --fmad=false`
compile gates pass without executing GPU 1. CPU throughput passes at 4,842
steps/s against the 3,858.9 floor and 4,062 baseline. The full native product
aggregate reaches credits in 480.04 seconds at 1,100,120 KiB with zero major
faults and zero swap. The quick pyramid passes in 55.78 seconds at 716,824 KiB
with one major fault and zero swap; only the two undistributed `.bsnp` stages
skip. At that checkpoint terminal Witch XP was open; the next checkpoint
closes the ordinary non-drinking player-credit case. Non-player lethal paths,
drinking/equipped XP, arbitrary NBT/persistence, held/status rendering, and
pixels remain open.

2026-08-09 ordinary Witch terminal-XP checkpoint:
`magma/trace/test_witch_terminal_xp.py` passes seven parked
real-Java/shared-CPU cases for the `3,1,1` base-five split, every EntityXPOrb
constructor field, Witch/Math/World cursor ordering, global IDs, terminal
Gaussian cache, and `doMobLoot=false`. `game/test_witch_terminal_xp_live`
locks controlled and ordinary product routes, three same-tick orb updates,
loaded-list order, retirement, exact 20-particle state, and gamerule
suppression. Scattered/runtime passes in 3.49 seconds at 115,200 KiB,
mob-live passes in 6.89 seconds at 172,748 KiB, and CPU throughput passes at
4,794 steps/s against the 3,858.9 floor. The full native aggregate reaches
credits in 492.81 seconds at 1,101,196 KiB with zero major faults or swap. The
quick pyramid passes in 53.70 seconds at 713,996 KiB with two major faults,
zero swap, and only the two undistributed `.bsnp` skips. Java 8 and the shared
`sm_120 --fmad=false` compile probe pass without GPU execution.
Drinking/equipped Witch equipment drops and XP bonus, non-player lethal
routes, NBT resume, held/status rendering, and pixels remain open.

2026-08-09 represented equipped-Witch death checkpoint:
`magma/trace/test_witch_equipped_death.py` passes eight parked
real-Java/shared-CPU cases covering held-potion drop/no-drop, Looting zero
through three, XP totals 6/7/8 and splits `3,3`/`7`/`7,1`, exact item/orb
constructor fields, all Witch/Math/World/Gaussian cursors, global IDs, and
`doMobLoot=false`. `game/test_witch_equipped_death_live` begins at lethal
player damage and locks potion EntityItem metadata/state, independent
equipment-drop and XP draws, controlled and ordinary product tick-20 routes,
same-tick orbs, loaded order, 20 exact particles, and fixed-item-capacity
atomicity. Scattered/runtime passes in 1.86 seconds at 82,120 KiB and mob-live
passes in 6.98 seconds at 172,728 KiB, with zero major faults or swap. CPU
throughput passes at 4,615 steps/s against the 3,858.9 floor. Java 8 and the
shared `sm_120 --fmad=false` compile probe pass without GPU execution. The
quick pyramid passes in 55.91 seconds at 716,844 KiB with zero major faults or
swap; only the two undistributed `.bsnp` stages skip. Non-player lethal paths,
arbitrary equipment/NBT resume, held/status rendering, and pixels remain open.

2026-08-09 ordinary drowning-killed Witch checkpoint:
`magma/trace/test_witch_drowning_death.py` passes seven parked
real-Java/shared-CPU cases covering the exact 48-float bubble prefix, no-
attacker feedback, Looting-0 table drops, zero through three emitted stacks,
`doMobLoot=false`, exact EntityItem fields, no held-potion drop, no terminal
XP, same-tick death fields, all Witch/Math/World cursors, global IDs, and the
terminal Gaussian cache. `game/test_witch_drowning_death_live` reaches that
boundary through actual water/air state in the ordinary product tick and locks
`deathTime=1`, status/death-sound order, exact drops, no equipment/orbs, 20
terminal particles, gamerule suppression, and fixed-item-capacity atomicity.
Scattered/runtime passes in 3.41 seconds at 115,200 KiB with three major faults
and zero swap; mob-live passes in 6.86 seconds at 172,860 KiB with zero major
faults or swap. CPU throughput passes at 5,009 steps/s against the 3,858.9
floor and 4,062 baseline. Java 8 builds. The focused comparator itself passes
in 0.19 seconds at 30,252 KiB. The quick pyramid passes in 53.37 seconds at
716,872 KiB with two major faults, zero swap, and only the two undistributed
`.bsnp` stages skipped. Other non-player lethal sources, arbitrary equipment/
NBT resume, held/status rendering, and pixels remain open.

2026-08-09 ordinary falling-anvil-killed Witch checkpoint:
`magma/trace/test_witch_source_death.py` contributes seven falling-anvil rows
to a 14-row parked real-Java/shared-CPU gate covering fresh-hit feedback,
Looting-0 table drops,
zero through three emitted stacks, `doMobLoot=false`, exact EntityItem fields,
no held-potion drop, no terminal XP, same-boundary death state, all
Witch/Math/World cursors, global IDs, and the terminal Gaussian cache.
`game/test_witch_anvil_death_live` drives the actual falling-block collision
callback into an ordinary drinking Witch, continues through the product mob
tick to `deathTime=1`, and locks exact events, the no-credit terminal tail,
gamerule suppression, and fixed-item-capacity atomicity. The comparator passes
in 0.20 seconds at 30,252 KiB and the live gate in 0.02 seconds at 18,744 KiB.
The header-rebuilt full runtime regression passes in 329.56 seconds at 505,780
KiB with zero major faults and includes the older controlled passive anvil
fixtures. Scattered/runtime passes in 3.38 seconds at 115,200 KiB, mob-live in
6.76 seconds at 172,688 KiB, and CPU throughput passes at 4,883 steps/s against
the 3,858.9 floor and 4,062 baseline. Other non-player sources, broader anvil
target semantics, arbitrary equipment/NBT resume, held/status rendering, and
pixels remain open. Java 8 builds. The quick pyramid passes in 52.45 seconds
at 717,012 KiB with two major faults, zero swap, and only the two
undistributed `.bsnp` stages skipped.

2026-08-09 ordinary periodic-ON_FIRE Witch checkpoint:
The same `magma/trace/test_witch_source_death.py` gate passes seven additional
ON_FIRE rows, for 14 source rows total. They lock fire 20-to-19, same-tick hurt
and death fields, zero through three Looting-0 stacks, exact constructors,
feedback, all cursors and IDs, no equipment/orbs, terminal particles, and
gamerule suppression. `game/test_witch_burning_death_live` reaches that
boundary through the ordinary product tick and adds Fire Resistance, exact
phase order, the no-credit tail, and fixed-item-capacity atomicity. The
combined comparator passes in 0.20 seconds at 30,252 KiB and the focused live
gate in 0.01 seconds at 18,728 KiB. Scattered/runtime passes in 3.37 seconds at
115,200 KiB, mob-live in 6.75 seconds at 173,232 KiB, and CPU throughput at
4,741 steps/s against the 3,858.9 floor and 4,062 baseline. The header-rebuilt
full runtime gate passes in 327.96 seconds at 506,204 KiB with zero major
faults. The quick pyramid passes in 53.00 seconds at 716,904 KiB with one
major fault and only the two undistributed `.bsnp` stages skipped. Other
non-player sources, arbitrary equipment/NBT resume, held/status rendering,
and pixels remain open.

2026-08-09 ordinary lava-killed Witch checkpoint:
`magma/trace/test_witch_source_death.py` now passes 28 parked
real-Java/shared-CPU rows, seven each for falling anvil, ON_FIRE, direct lava,
and ON_FIRE-before-LAVA. They lock the exact lava AABB, raw-four damage,
fire-300 floor, fall-distance halving, same-tick hurt/death state, zero through
three Looting-0 stacks, constructors, feedback, all cursors/IDs, no
equipment/orbs, and terminal particles. The combined case proves the nonlethal
ON_FIRE hurt sound precedes a silent immunity-window LAVA kill.
`game/test_witch_burning_death_live` reaches direct and combined contact through
actual world lava and adds Fire Resistance plus full-item-pool atomicity. The
comparator passes in 0.46 seconds at 34,560 KiB, the live gate in 0.02 seconds
at 18,728 KiB, scattered/runtime in 5.22 seconds at 180,272 KiB, and mob-live
in 6.78 seconds at 172,128 KiB. CPU throughput passes at 5,024 steps/s against
the 3,858.9 floor and 4,062 baseline. The full runtime gate passes in 325.65
seconds at 506,028 KiB with zero major faults. Java 8 builds. The quick pyramid
passes in 56.66 seconds at 716,928 KiB with five major faults and only the two
undistributed `.bsnp` stages skipped. Block-collision IN_FIRE and other
non-player sources remain open.

2026-08-09 ordinary block-collision IN_FIRE Witch checkpoint:
`magma/trace/test_witch_source_death.py` now passes 42 parked
real-Java/shared-CPU rows, seven each for falling anvil, ON_FIRE, direct LAVA,
ON_FIRE-before-LAVA, direct IN_FIRE, and full-product-tick IN_FIRE. The new
rows lock the contracted FIRE/LAVA callback AABB, raw-one damage, cold fire
-1-to-160 transition, resistance/immunity behavior, exact base ambient RNG and
audio pitch, no-credit Looting-0 drops, events, particles, constructors, IDs,
and every cursor. `game/test_witch_burning_death_live` reaches actual FIRE over
NETHERRACK and adds the following dead tick, wet/resistance semantics, and
full-item-pool atomicity. The comparator passes in 0.61 seconds at 30,252 KiB,
the live gate in 0.03 seconds at 18,728 KiB, scattered/runtime in 1.72 seconds
at 82,112 KiB, and mob-live in 6.94 seconds at 172,168 KiB. CPU throughput
passes at 4,937 steps/s against the 3,858.9 floor and 4,062 baseline. Full
runtime passes in 346.11 seconds at 505,888 KiB with zero major faults. Java 8
builds. The quick pyramid passes in 53.55 seconds at 716,900 KiB, with every
available step green and only the two undistributed `.bsnp` stages skipped.
Rain/mixed-water contact and other non-player sources remain open.

2026-08-09 ordinary cactus-contact Witch checkpoint:
The source comparator now passes 56 parked real-Java/shared-CPU rows, adding
seven direct and seven full-product-tick CACTUS cases to the prior 42. They
lock the 0.001-inset callback scan at cactus's 15/16 collision top, raw-one
damage, post-move death/timer state, base ambient/status RNG composition,
Looting-0 drops, feedback, particles, constructors, IDs, and all cursors. The
focused live gate stages actual cactus over sand and adds fixed-item-capacity
atomicity. The comparator passes in 0.56 seconds at 30,252 KiB, focused live
in 0.03 seconds at 18,752 KiB, scattered/runtime in 3.39 seconds at 115,200
KiB, and mob-live in 6.82 seconds at 173,124 KiB. CPU throughput passes at
5,000 steps/s against the 3,858.9 floor and 4,062 baseline. Java 8 builds.
The quick pyramid passes in 55.11 seconds at 716,848 KiB, with every available
step green and only the two undistributed `.bsnp` stages skipped.

2026-08-09 ordinary still-water plus block-fire Witch checkpoint:
The source comparator now passes 63 parked real-Java/shared-CPU rows. Seven
new full-product-tick cases span adjacent WATER and supported FIRE blocks and
lock water-first extinguishing, raw-one IN_FIRE damage, wet ignition
suppression, fire zero, exact no-credit loot, constructors, feedback,
particles, IDs, and every cursor. The focused live gate reaches the same
actual-world block scans. The comparator passes in 0.70 seconds at 30,252 KiB,
focused live in 0.04 seconds at 18,764 KiB, scattered/runtime in 3.46 seconds
at 115,200 KiB, and mob-live in 6.91 seconds at 171,816 KiB. CPU throughput
passes at 4,955 steps/s against the frozen 3,858.9 floor and 4,062 baseline.
Java 8 builds. The quick pyramid passes in 54.34 seconds at 716,928 KiB with
zero failures, five major faults, no swap, and only the two undistributed
`.bsnp` stages skipped.

2026-08-09 ordinary rain-wet block-fire Witch checkpoint:
The source comparator now passes 77 parked real-Java/shared-CPU rows. Fourteen
new full-product-tick cases cover open-rain and roofed-rain FIRE contact and
lock the exact feet/full-height precipitation probes, raw-one damage,
post-callback extinguishing versus fire 160, no-credit loot, constructors,
feedback, particles, IDs, and every cursor. The focused actual-world gate
reaches both production weather and sky-column outcomes. The comparator passes
in 0.73 seconds at 30,252 KiB, focused live in 0.06 seconds at 18,728 KiB,
scattered/runtime in 3.43 seconds at 115,200 KiB, and mob-live in 6.71 seconds
at 171,880 KiB. CPU throughput passes at 4,960 steps/s against the frozen
3,858.9 floor and 4,062 baseline. Java 8 builds. A clean header-rebuilt
`test-game` passes in 481.99 seconds at 1,100,948 KiB with one major fault and
no swap. The quick pyramid passes in 55.30 seconds at 716,840 KiB with zero
failures, two major faults, no swap, and only the two undistributed `.bsnp`
stages skipped.

2026-08-09 ordinary Witch flowing-water and entry checkpoint:
The source comparator now passes 84 parked real-Java/shared-CPU rows. Seven
new complete product ticks lock normalized +0.014 flowing-water acceleration,
water-first fall reset, and the later lethal lava boundary. A separate
seven-seed entry comparator bit-locks the non-first dry-to-water HOSTILE splash
event, position, motion-scaled volume, pitch, motion/state, 26 particle calls,
and final entity RNG cursor. The product consumes the exact particle RNG but
also exports all 26 exact descriptors through the bounded runtime and water
renderer; constructor-private visual entropy remains deterministic rather than
Java-pixel exact. The source comparator passes in 0.99 seconds at 34,560 KiB
and the entry comparator in 0.29 seconds at 31,680 KiB. Focused live passes in
0.11 seconds at 20,216 KiB, water rendering in 0.11 seconds at 36,000 KiB,
player movement runtime in 0.18 seconds at 20,176 KiB, audio in 0.51 seconds
at 63,360 KiB, scattered/runtime in 3.46 seconds at 115,200 KiB, and mob-live
in 6.84 seconds at 172,964 KiB. Java 8 builds. A clean
header-rebuilt `test-game` passes in 484.66 seconds at 1,101,224 KiB with ten
major faults and zero swap. CPU throughput passes at 5,030 steps/s against the
frozen 3,858.9 floor and 4,062 baseline. The quick pyramid passes in 55.36
seconds at 715,468 KiB with zero failures, zero major faults, zero swap, and
only the two undistributed `.bsnp` stages skipped.

2026-08-09 ordinary Witch in-wall suffocation checkpoint:
Registry schema `qrl.blockstate_props.v4` captures the exact 1.11.2
`causesSuffocation` predicate over all 4,096 legacy states and pins digest
`57911f7d3a42bd1e585d3cca75283fe500fbfcb2308c3f15127837a2c56433f6`.
One generated lookup now drives both the player overlay and the ordinary Witch
server path. The source comparator passes 91 real-Java/shared-CPU rows,
including seven exact eight-eye-sample IN_WALL deaths and the post-base-death
Witch living-status RNG phase. State capsule, block registry, focused live,
scattered/runtime, mob-live, particles, and Java 8 build all pass. A clean
rebuilt `test-game` passes through the final route in about 489 seconds. CPU
throughput passes at 4,748 steps/s against the 3,858.9 floor and 4,062
baseline. The quick pyramid passes in 56.78 seconds at 717,036 KiB with zero
failures, zero major faults, zero swap, and only the two undistributed `.bsnp`
stages skipped.

2026-08-09 ordinary Witch stone/hay landing checkpoint:
The source comparator now passes 105 parked real-Java/shared-CPU rows. Fourteen
new complete product ticks cover stone and hay landings and lock the pre-move
fall distance, exact position/motion and late death state, Looting-0 drops,
constructors, IDs, sound/status order, BLOCK_DUST packet descriptor, and every
Witch/Math/World cursor. Stone produces count 40 with state parameter 1; hay
uses the one-fifth damage multiplier and produces count 80 with parameter 170.
The runtime gate verifies hostile fall, Witch death, support-block audio, and
the descriptor in the global bounded streams.

The comparator passes in 0.88 seconds at 30,252 KiB, focused live in 0.12
seconds at 20,160 KiB, audio in 0.37 seconds at 63,420 KiB, and mob-live in
6.45 seconds at 172,336 KiB. CPU throughput passes at 4,942 steps/s against
the frozen 3,858.9 floor and 4,062 baseline. Java 8 builds. The full game
aggregate passes through fresh spawn to credits in 478.45 seconds at 1,101,084
KiB with one major fault and zero swap. The quick pyramid passes in 54.57
seconds at 716,848 KiB with zero failures, zero major faults, zero swap, and
only the two undistributed `.bsnp` stages skipped.

2026-08-09 ordinary Witch landing-completion checkpoint:
The lethal source comparator now passes 112 parked real-Java/shared-CPU rows,
adding seven big-stone landings to the prior small-stone and hay families. A
new 28-row nonlethal comparator covers Jump Boost II, non-sneaking living
slime bounce, seeded farmland trample/rejection, and `mobGriefing=false` while
locking raw motion/position, effects, events, BLOCK_DUST, support state, and
Witch/World RNG. It exposed and closed visible potion-effect server RNG
ordering before Witch ambient logic.

The source and landing comparators pass in 1.01 and 0.22 seconds at 30,252
KiB. Focused live, mob-live, scattered/runtime, particles, audio, and Java 8
build all pass. CPU throughput is 4,104 steps/s against the frozen 3,858.9
floor and 4,062 baseline. A clean full `test-game` passes through fresh spawn
to credits in 484.76 seconds at 1,100,928 KiB with no major faults or swap.
The quick pyramid passes in 65.98 seconds at 715,480 KiB with zero failures
and only the two undistributed `.bsnp` stages skipped. GPU 1 was not executed.

2026-08-09 shared living-slime checkpoint:
The non-ridden ordinary-living product path now applies the same bounded slime
landing contact used by Witch to every represented living type. A six-row
real-Java/shared-CPU comparator locks raw position/motion, collision flags,
ground/fall state, support state, and the exact 80-count BLOCK_DUST descriptor
for zombie, sheep, and Witch across full bounce and low-speed walking-damping
cases. Product ticks independently prove the hostile and passive paths at the
exact `0.01960000038146973` Y-motion tail and export the same particle batch.
The implementation reuses the resident collision list, adds no world reads or
allocation to the contact path, and caps support contacts at 32.

The comparator passes in 0.15 seconds at 30,252 KiB, the unchanged 28-row
Witch landing matrix in 0.20 seconds at 30,252 KiB, mob-live in 6.74 seconds
at 173,188 KiB, scattered/runtime in 3.57 seconds at 115,200 KiB, and the
particle renderer in 0.13 seconds at 37,440 KiB. Java 8 builds. CPU throughput
passes at 4,991 steps/s against the frozen 3,858.9 floor and 4,062 baseline.
The quick pyramid passes in 54.00 seconds at
716,896 KiB with zero failures and only the two undistributed `.bsnp` stages
skipped. GPU 1 was not executed.

2026-08-09 ordinary-living nonlethal landing checkpoint:
The shared product path now covers exact ordinary nonlethal small/big stone
falls, hay reduction, and Jump Boost II avoidance for 13 types: zombie, zombie
villager, skeleton, wither skeleton, creeper, spider, cave spider, pigman,
silverfish, sheep, pig, cow, and villager. A 58-row real-Java/native comparator
retains the six slime controls and locks 52 new rows through raw travel state,
health/timers/effect state, exact event order and pitch, both RNG cursors,
creeper fuse, support state, and the 50/80-count BLOCK_DUST descriptors.
Product ticks cover all 13 types on small stone, representative big-stone/hay/
Jump-Boost branches, and assert that source-less FALL does not anger pigmen or
panic passives. The audio gate proves generic passive fall, type hurt, and
neutral support playback order. Enderman remains a measured special case
because Java's damage override teleports and consumes additional RNG/audio.

The 58-row comparator passes in 0.33 seconds at 30,252 KiB, the unchanged
28-row Witch landing matrix in 0.17 seconds at 30,252 KiB, mob-live in 8.73
seconds at 172,004 KiB, particles in 0.16 seconds at 37,440 KiB, audio in 0.52
seconds at 65,772 KiB, and scattered/runtime in 4.49 seconds at 115,200 KiB.
Java 8 builds. CPU throughput passes at 4,946 steps/s against the frozen
3,858.9 floor and 4,062 baseline. The quick pyramid passes in 54.69 seconds at
714,340 KiB with zero failures, seven major faults, zero swap, and only the two
undistributed `.bsnp` stages skipped. GPU 1 was not executed.

2026-08-10 fresh-object NBT continuation checkpoint:
The real server now replaces saved NoAI entities with newly constructed
`EntityList.createEntityFromNBT` instances before capture. The capsule/native
gate passes all 23 plain native living classes for 20 ticks, comparing raw
position/motion, exact AABBs, timers, private RNG, and promoted subclass state.
The direct source-derived NoAI gate passes 19 classes. The legal survival route
uses live gravel-drop RNG rather than a coordinate prediction. Focused gates,
the quick pyramid, and the CPU performance floor pass; GPU 1 remains untouched
while shared. A clean full `make -C magma test-game` passes in 10:15.22 at
1,101,200 KiB peak RSS with zero swap, including the legal route through
credits.

2026-08-11 cross-class NBT and saved-village checkpoint:
The real fresh-object boundary now reconstructs all 23 plain NoAI living
classes plus 23 interleaved XP orbs while preserving the complete Java loaded
list around a non-reconstructed Item sentinel. The neutral capsule restores
the admitted saved `VillageCollection` state, including collection/tick/mating
clocks, ordered doors, UUID reputation, centers, population, and radius. The
runtime ports the Java village tick and World RNG order for the fewer-than-ten-
villagers, zero-golem subset; admission rejects a saved golem or an unproven
door block. Direct Java/native village state, real-client 20-tick NBT, capsule,
and Java 8 gates pass. CPU throughput is 4,160 steps/s against the 4,062
baseline and 3,858.9 floor. The quick pyramid passes all available stages with
only two undistributed `.bsnp` skips. A clean broad `make -C magma test-game`
passes through credits in 11:05.45 at 1,100,928 KiB peak RSS with no major
faults or swap. GPU 1 was untouched.

2026-08-13 portal random-tick spawn checkpoint:
The direct real-Java/native gate now passes adult and baby pigman spawn, both
existing- and constructed-chicken jockey paths, miss and disabled-gamerule
controls, and one-, two-, and five-tick continuation. It compares exact entity,
world, UUID, and entity-ID streams; complete living/equipment state; loaded
order; passenger graph and pose; lower-priority AI; and chicken animation. The
five-tick native route writes and reloads a runtime checkpoint after tick two
and remains identical to uninterrupted Java. Focused portal and mob-live
regressions pass. Java 8 builds. CPU throughput and the broad sweep are recorded
below after measurement; GPU 1 remains untouched.

2026-08-13 Ender Chest screen/TESR checkpoint:
The live player-owned inventory now has a deterministic populated real-client
fixture. Java A/B is exact, and the native panel matches all 116,720 owned
pixels exactly, including the Ender Chest title, item stacks/counts, and hover
state. The in-world tile has closed/open/no-tile same-scene fixtures with its
client lid fields re-pinned immediately before both atomic renders and display
particles explicitly cleared. Both keyframes have exact Java A/B and zero
native subject pixel above max-channel 25; the bounded fixed-function maximum
is 19. Both gates include mutation negative controls and run as
`ENTITY_GATE_ENDER_CHEST_GUI_ONLY=1` or
`ENTITY_GATE_ENDER_CHEST_WORLD_ONLY=1` with
`verify/ui_entities/run_oracle_gate.sh`. Java 8 and the native candidate build
pass. These additions allocate only in oracle/capture startup and do not alter
the production tick hot path. GPU 1 remains untouched.

2026-08-13 wooden Chest TESR checkpoint:
Ordinary and trapped single/double chests now render through the live tile pass
with their four jar textures, exact ModelChest/ModelLargeChest dimensions,
interpolated lids, metadata rotations, and Java double-chest translations.
Six subject states plus a no-chest control have byte-exact Java A/B frames and
atomic one- or two-tile lid pins. The four single states have zero owned pixel
over four channels; X-double has one bounded pixel (max 14); Z-double has two
 exact-coordinate minified edge/texel ties and no other owned pixel over four.
The focused gate is `ENTITY_GATE_CHEST_WORLD_ONLY=1` with
`verify/ui_entities/run_oracle_gate.sh`; its mutation control and pinned
residual coordinates fail closed.

2026-08-13 Shulker Box runtime/save/UI/TESR checkpoint:
All 16 colors and six facings now use the live 27-slot tile path with exact
activation obstruction, open/close animation, changing collision, entity push,
comparator state, hopper routing and nested-box rejection, audio, teardown, and
native checkpointing. A strict real-Java/capsule/native fork starts at raw
binary32 progress 0.3 to 0.4 while OPENING and matches viewer count, status,
both progress fields, inventory, and the OPENED clamp for seven ticks. The
populated screen has exact Java A/B and matches all 116,792 opaque owned pixels.
Seven zero-noise Java A/B world keyframes cover color, facing, and progress;
only six pixels exceed channel delta 4 across the set, three pinned minified
edge ties above 25. Run `magma/trace/test_shulker_box_capsule.py`,
`ENTITY_GATE_SHULKER_BOX_GUI_ONLY=1`, and
`ENTITY_GATE_SHULKER_BOX_WORLD_ONLY=1` with
`verify/ui_entities/run_oracle_gate.sh`. Java 8 and the focused native tests
pass. No idle scan or allocation was added, and GPU 1 was untouched.

2026-08-13 chest-family transient continuation checkpoint:
The neutral capsule no longer rejects open ordinary/trapped chests. Four
real-Java/native forks cover opening ordinary and closing trapped single and
double chests. All halves match viewer count, both raw binary32 lid fields,
the private `ticksSinceSync` counter, inventory, pairing, and terminal clamps
for seven ticks. The existing 16-tick trapped-chest use/redstone lifecycle
also passes all 29 state families plus exact raw block/light gates. Native
opening and closing threshold tests pin the exact sound identity, source,
volume/pitch, and one World RNG draw. Run
`magma/trace/test_chest_capsule.py`,
`make -C magma test-ender-chest-native`, and the focused
`redstone_trapped_chest_viewer_power_open_close_seed_0` oracle-matrix case.
The trace builder was resynchronized with the production object list so this
focused path links again. CPU throughput passes at 3,938 steps/s against the
3,858.9 floor. The chest tick uses constant-time neighbor block probes rather
than a dense pairwise tile scan. GPU 1 was untouched.

2026-08-13 Large Chest container/UI checkpoint:
Ordinary and trapped double chests now open a true 54-slot live container.
BlockChest's north/west-first half order is preserved even when the player
clicks the south/east half; pickup, deposit, quick-move, throw, viewer close,
and physical tile persistence cover both 27-slot halves. Recorded 90-slot
GuiChest rows map all 54 chest, 27 main-inventory, and nine hotbar slots into
the native replay screen. A fresh real-client populated fixture has zero Java
A/B drift, and native matches all 154,736 owned panel pixels exactly,
including the six-row generic_54 composite, both-half items, title, and
one-GUI-pixel hover border. Run `magma/game/test_container_live.sh`,
`magma/game/test_screen.sh`, and
`ENTITY_GATE_LARGE_CHEST_GUI_ONLY=1 verify/ui_entities/run_oracle_gate.sh`.
This adds no tick-time allocation or idle work; the added fixed replay slots
are accessed only while a GUI/tape is active. With the host at load average
101, unpinned sampling was scheduler-contaminated; two independent pinned-core
runs pass at 4,070 and 4,043 steps/s against the 3,858.9 floor. GPU 1 was
untouched.

2026-08-13 Beacon runtime/UI/TESR checkpoint:
Beacon now has exact pyramid validation, colored beam segmentation, payment
and primary/secondary effect rules, container state, NBT/native continuation,
its jar block model, and the two-pass beam tile renderer. The populated Beacon
screen has byte-exact Java A/B and matches all 201,336 owned panel pixels.
The HUD-hidden world fixture also has byte-exact Java A/B. Java-compatible
far-to-near translucent quad sorting per 16-block render section, the
15-section camera-movement re-sort budget, and reverse visible-section
submission remove the previous 189-pixel stained-glass/beam ordering error.
Across 22,882 owned world pixels, four isolated pixels remain above
max-channel 25: two measured registration pixels at the beam top, one
texel-selection pixel at the glass boundary, and one shading-offset pixel on
the pyramid. They are pinned by the focused bounded gate. Run
`make -C magma test-beacon-native`, `make -C magma test-mesh`,
`make -C magma test-block-registry`, and the
`ENTITY_GATE_BEACON_GUI_ONLY=1` or `ENTITY_GATE_BEACON_WORLD_ONLY=1` variants
of `verify/ui_entities/run_oracle_gate.sh`. Translucent sort scratch is fixed
per world, with no per-frame allocation. A fresh two-state candidate render
takes about 0.99 seconds and peaks near 210 MB RSS. GPU 1 was untouched.

2026-08-14 Structure Block runtime/template checkpoint:
Structure Block now has exact four-mode models and live tile state, corner
detection, cached save/load/unload, all-registry mirror/rotation transforms,
stable Template ordering, structure-void omission, size synchronization,
integrity RNG, redstone edges, and native checkpoint continuation. Template
placement follows Java's barrier, flags-4/flags-2, tile-NBT, then ordered
neighbor lifecycle. Represented tile payloads are copied, including arbitrary
ItemStack NBT handles and player-skull profile NBT. A parked real 1.11.2 server
and native runtime agree on defaults, sanitation, corners, transformed states,
a nonempty transformed chest, integrity output, and redstone edges. The native
test additionally proves stale destination tile data is cleared and hostile
Structure Template checkpoint metadata is rejected. Run
`make -C magma test-structure-block-native` and, with the oracle on its chosen
port, `QRL_PORT=<port> make -C magma test-structure-block-oracle`. This adds no
tick scan or idle allocation; storage is allocated only after first Structure
Block use. GPU 1 was untouched.

2026-08-14 TNT minecart render checkpoint:
The live and capture renderers now apply Java's quartic fuse-under-ten display
scale, FaceBakery face UV orientation, exact entity-id sub-pixel registration,
and the alternating five-tick white overlay. Real 1.11.2 captures at fuse 80,
79, 4, 5, and -1 prove both lit/dark boundaries and the late swell. They also
lock an easy-to-miss 1.11.2 quirk: `renderBlockBrightness` replaces RGB on an
opaque baked vertex, so the computed `glColor` fade alpha is not observable
and the active flash is opaque white. All six Java A/B fixtures are byte
stable. The native same-scene lane passes its mutation control, semantic flash
checks, and fixed residual budgets; fuse 80 has one pixel above channel delta
25 and the darker/scaled states retain only the pinned raster coverage tail.
Run `magma/game/test_item_render.sh` and
`ENTITY_GATE_MINECART_TNT_ONLY=1 verify/ui_entities/run_oracle_gate.sh`.
The added work is proportional only to visible TNT carts and no new tick scan
or allocation was added. GPU 1 was untouched.

2026-08-14 complete minecart lineup checkpoint:
The same zero-noise real-client profile now pins all six non-TNT variants:
empty, chest, furnace, hopper, spawner, and command. The renderer reproduces
the hidden +90-degree display-block transform, world-space item lighting,
`BlockPart.setDefaultUvs` coordinate-derived rectangles and FaceBakery's
0.001 UV contraction. Chest carts use the real ModelChest parts and standalone
texture transform; command carts use the pinned first animated texture frame
with `cube_directional` face rotations. Empty, furnace, and command have no
canonical pixel above channel delta 4; chest has one isolated shading sample;
hopper retains two measured low-amplitude shading strips (31 pixels, maximum
delta 6); spawner has two isolated shading samples. The fixed budgets and
mutation control pass in
`ENTITY_GATE_MINECART_VARIANTS_ONLY=1 verify/ui_entities/run_oracle_gate.sh`.
No new tick scan or allocation was added, and GPU 1 was untouched.

2026-08-14 Slime/Magma strict render checkpoint:
Four Slime and four Magma Cube real-client fixtures now cover sizes 1, 2, and
4 plus a pinned nonuniform squish keyframe. The oracle pins the separate
`renderYawOffset` used by `RenderLivingBase`, verifies that one pinned entity
was rendered in each atomic frame, and produces byte-exact Java A/B pairs.
Native now reproduces `RenderSlime`'s gel lightmap context,
`prepareScale`/ModelSlime translation ordering, and the legacy
`GL_RESCALE_NORMAL` result on a nonuniformly scaled ModelMagmaCube. The Magma
size-2, size-4, and squish subjects are at the channel-one floor; size 1 has
two isolated hard edge samples. Slime's remaining gel-pass
shading/texel-selection tail is classified and independently bounded for each
state. The same-scene gate validates its mutation negative control and runs as
`ENTITY_GATE_SLIME_MAGMA_ONLY=1 verify/ui_entities/run_oracle_gate.sh`.
`magma/game/test_entity_render.sh`, `magma/game/test_item_render.sh`, and both
minecart pixel lanes remain green. The changes add no tick scan or allocation;
the rescaled-normal correction is proportional only to visible squashed
Slime/Magma models. GPU 1 was untouched.

2026-08-14 living-effect and XP capsule checkpoint:
The real fresh-object NBT boundary now carries active effects for all 34 plain
native living classes, including amplifier, duration, ambient and particle
flags, Health Boost maximum health, current health, and absorption. The same
strict gate interleaves 29 XP orbs and one Item sentinel in Java loaded order.
XP capsules now retain the authoritative six-edge AABB; reconstructing it from
the center had caused one saved large-coordinate orb to differ by one double
ULP after movement. The corrected 20-tick Java/capsule/native continuation
matches raw position, motion, AABBs, health, timers, effects, subclass state,
and update order. State-capsule selftest, Java 8 compile, the native runtime
aggregate, and mob-live regressions pass. This adds only fixed-size restore
work at capsule load and no steady-state allocation or new world scan. GPU 1
was untouched.

2026-08-14 Glowing outline checkpoint:
Active effect 24 and persisted glowing flags now reach represented render
views. The capture and interactive paths implement the dedicated entity mask,
two-pass 1.11.2 outline shader, and final source-alpha composition. Invisible
glowing living models are restored only in that pass. RenderLivingBase's
cull-disabled state and the Slime-only translucent gel layer are represented.
The Magma same-scene control/Glowing pair has byte-exact Java A/B captures and
zero final native channel difference over every affected pixel. A separate
Slime stress pair has an exact outline bounding box and a mutation-tested
ceiling over its pxdiff-classified fixed-function shading-offset tail. The
gate is `ENTITY_GATE_GLOWING_ONLY=1
verify/ui_entities/run_oracle_gate.sh`. Entity rendering, overlay, Armor Stand,
and the broad game aggregate pass. CPU throughput is 3,961 steps/s against the
frozen 3,858.9 floor and 4,062 baseline. GPU 1 was untouched.

2026-08-14 Luck loot-context checkpoint:
The native loot engine now accepts an explicit 1.11.2 `LootContext` through
the complete `fillInventory` path, and single deferred chests receive the
opening player's Luck. The double-chest null-player materialization edge is
preserved. A direct real-Java gate matches quality-adjusted weights,
Luck-scaled bonus rolls, list order, shuffled slots, and counts over 64 seeded
cases. Its reflection census also proves that all 80 non-fishing built-in
tables have zero quality and bonus rolls, so their output is Luck-invariant.
Generated chest, unopened-break, and automation regressions pass. The gate is
`bash magma/game/test_luck_loot.sh`. Arbitrary custom tables remain under
`ITEM-07`. GPU 1 was untouched.

2026-08-14 dispenser/dropper/hopper container checkpoint:
The playable product now opens its represented dispenser, dropper, and hopper
tile inventories with the real jar panels, exact titles, and exact 1.11.2 slot
coordinates. The actual Java `ContainerDispenser` and `ContainerHopper` are the
oracle for all container-order coordinates plus pickup, right-half pickup,
deposit, merge, and quick-move results. Native matches every emitted row and
also gates throw/close routing, rich-stack preservation, adjacent comparator
notification, distance close, and open-container checkpoint/reload. The same
test exposed a pre-existing comparator admission omission for hopper, brewing
stand, and Shulker Box inventory fullness; all three are now admitted and have
focused click-notification cases. `test_container_live`, all 448 brewing
checks, hopper automation, Shulker Box runtime, and chest/deferred-loot gates
pass. The full `make -C magma test-game` aggregate passes through credits in
14:47.68 at 2,341,936 KiB peak RSS with zero major faults and zero swap. The
frozen CPU guard passes at 4,059 steps/s against the 3,858.9 floor and 4,062
baseline. The implementation adds no idle scan or allocation. GPU 1 was
untouched.
The populated screen lane also records real `GuiDispenser` and `GuiHopper`
frames with byte-exact Java A/B controls. Native matches all 116,792 owned
dispenser pixels, 116,792 dropper pixels, and 93,560 hopper pixels exactly;
every maximum channel delta is zero. The alpha-owned mask is mutation-tested.
Run it with
`ENTITY_GATE_STATIC_CONTAINER_GUI_ONLY=1 verify/ui_entities/run_oracle_gate.sh`.

2026-08-14 Furnace/Brewing populated GUI checkpoint:
The deterministic real-client fixtures use actual `GuiFurnace` and
`GuiBrewingStand` screens with populated tile inventories. They assert exact
tile fields, including Furnace `[800,1600,100,200]` and Brewing Stand
`[200,10]`, before accepting a frame. Java A/B is byte-identical. Native
matches all 116,792 owned pixels for each screen with maximum channel delta
zero, including furnace fuel/cook progress, brewing bubbles/progress, counts,
and the layered empty-potion overlay tint. The first strict run caught a bad
oracle-fixture mutation order and the missing potion layer; both are retained
by mutation-tested gates. `test_furnace_live` passes 1,452 checks,
`test_brewing_live` passes 448 checks, the prior static-container exact lane
remains zero-delta, and the CPU guard passes at 4,015 steps/s against the
3,858.9 floor and 4,062 baseline. Run the dedicated lane with
`ENTITY_GATE_PROCESSING_CONTAINER_GUI_ONLY=1 verify/ui_entities/run_oracle_gate.sh`.

2026-08-14 Crafting/Anvil/Merchant populated GUI checkpoint:
The real-client fixtures use actual `GuiCrafting`, `GuiRepair`, and
`GuiMerchant` screens. Their actual Java containers compute and assert Blaze
Rod to Blaze Powder crafting, an Anvil rename with maximum cost 1, and a
two-offer Villager trade with populated inputs and output. Java A/B is
byte-identical for all three states. Native matches every one of the 116,792
owned pixels per screen with maximum channel delta zero. The Anvil's first
strict run exposed 8,332 wrong pixels: its active name field, invalid overlay
path, cost renderer, and extra Inventory label were incomplete. After those
fixes, the remaining 1,256 pixels identified the exact 1.11.2 localization
`Enchantment Cost: 1`; the final result is zero-delta. Crafting and Merchant
were zero-delta on their first strict runs. Screen, container, Anvil live, and
the full Villager trade oracle pass. Prior processing and static-container
pixel lanes remain zero-delta. The CPU guard passes at 4,068 steps/s against
the 3,858.9 floor and 4,062 baseline. Run the combined lane with
`ENTITY_GATE_STANDARD_CONTAINER_GUI_ONLY=1 verify/ui_entities/run_oracle_gate.sh`.

2026-08-14 Enchanting populated GUI checkpoint:
The real-client fixture uses the actual `GuiEnchantment` and
`ContainerEnchantment` with a Diamond Sword, 12 Lapis Lazuli, seed zero, and
15-bookcase power. Capture asserts offer levels `[8,13,30]`, clue IDs
`[34,17,21]`, clue levels `[1,2,2]`, exact generated runic line layout, and
both slots. The recorder re-pins the animated book at the framebuffer boundary,
so the byte-identical Java A/B pair cannot drift between state query and
capture. Native ports the seven-part `ModelBook`, texture, transforms, lighting,
and panel depth interaction and matches all 116,792 owned pixels with maximum
channel delta zero. Enchanting live and general screen tests pass; the combined
Crafting/Anvil/Merchant/Enchanting lane remains zero-delta. CPU throughput is
4,056 steps/s against the 4,062 baseline and 3,858.9 floor. Run the focused lane
with `ENTITY_GATE_ENCHANTING_GUI_ONLY=1
verify/ui_entities/run_oracle_gate.sh`.

2026-08-14 Furnace registry-completeness checkpoint:
The live Java client exports all 51 initialized smelting recipes plus
`getItemBurnTime` and `getSmeltingExperience` for all 392 item registry rows.
Native matches every recipe, all 392 fuel results at metadata 0/1/15, all
6,272 XP item/meta results, nine exact-meta/unknown negative controls, and four
mutation paths. Sixty-five items are valid fuels. Player output extraction
also locks both fractional-rounding branches, exact shared Math cursor
consumption, split-orb construction/order, and the zero-XP branch; hopper
extraction remains XP-free. The live
tick covers lava-bucket return and wet-sponge water-bucket conversion and uses
complete vanilla stack limits. `test_furnace_live` passes 1,470 assertions;
six fuel seed classes each match Java over 2,000 tick fields; smelting and
full-tick CPU equal CUDA; and the populated Furnace/Brewing pixel lane remains
zero-delta. CPU throughput passes at 4,221 steps/s against the 4,062 baseline
and 3,858.9 floor. Run the registry pins with
`verify/completeness/smelting_registry_gate.py` and
`verify/completeness/furnace_fuel_gate.py`.

2026-08-15 Furnace custom-name/save checkpoint:
The real Java fixture writes `TileEntityFurnace.CustomName`, captures the
authoritative display name and four timers, and cold-saves the world. The
fail-closed Anvil bridge imports that save with zero limitations; native state
re-emits the exact name and timers. Named placement, Structure copies, GUI
selection, and native checkpoint continuation have focused regressions. Rich
furnace inventory stacks also survive block retirement.

2026-08-15 InventoryHelper drop/save checkpoint:
A focused executable invokes the real 1.11.2 `InventoryHelper.spawnItemStack`
and compares raw native values for shared offsets, every 10..30 split, item
position and motion, EntityItem constructor RNG, IDs, and final RNG cursors.
Both an empty and a preloaded Gaussian cache pass exactly. The private static
InventoryHelper seed and cache now cross authoritative Java capture, isolated
save shadowing, hidden-state export/restore, capsule import, native state, and
checkpoint continuation. A real isolated save imported with zero limitations
and native replay reproduced seed `221388897376437`, empty cache, and cached
value bits exactly. A 27-slot count-64 fixture preserves all 1,728 items across
at most 189 entities. `test_inventory_helper_oracle.sh`, container, static
container, minecart, script-route, and native-checkpoint gates pass. Raising
the live item table from 48 to 256 changed a 200,000-tick headless run from
3.80 seconds/33,120 KiB RSS to 3.80 seconds/33,124 KiB RSS; the CPU throughput
gate remains above floor at 4,014 steps/s versus the 3,858.9 floor.

2026-08-15 Furnace output and boundary completion checkpoint:
The actual 1.11.2 `SlotFurnaceOutput` produces an exact 19-line native
transcript across iron with and without its achievement prerequisite, cooked
fish, and ordinary stone. Craft-stat mutation precedes XP-orb construction;
the Forge smelt event follows every orb; `acquireIron` or `cookFish` is attempted
last and persists only after `buildFurnace`. The native statistics writer
updates existing fields, appends newly created fields deterministically, and
preserves unrelated nested JSON bytes. A native checkpoint restores every
counter and both rich ordered smelt events. A second direct oracle writes and
reads real furnace NBT, then matches eight adjacent tick pairs for ignition,
burn 1 refueling, completion, blocked output, idle cooling, invalid-input
reset, extinguishing, and wet-sponge bucket replacement. It also proves that
Java reconstructs the unsaved current-item burn duration from the fuel slot.
The controlled-input comparator now captures InventoryHelper RNG and Gaussian
cache immediately before and after edits, eliminating ambient work from the
causal cursor check. Furnace, minecart, container, script, checkpoint,
save-slot, Java build, smelting registry, and fuel/XP gates pass. CPU throughput
passes at 4,028 steps/s against the 3,858.9 floor. GPU 1 was untouched.

2026-08-15 Container.slotClick checkpoint:
The actual Java/native comparator passes 149 rows across all seven ClickTypes,
including 98 paired ordinary-PICKUP states, rich tag/cap/metadata boundaries,
armor/offhand/horse rules, every take-only output family, furnace and brewing
`onTake` effects, close/drop order, and checkpointed cursor/drag/event/stat
state. The live GUI now follows Java's press/release ordering for drag,
double-click, and shift-double-click, including the same-backing-inventory
batch scan. `test_container_click_oracle.sh`, `test_container_live.sh`,
`test_screen.sh`, neighboring container regressions, native save/checkpoint
forks, and a clean `magma_game` build pass. CPU throughput passes at 3,935
steps/s against the 3,858.9 floor under heavy shared-host load; GPU 1 was not
used.

### Gate 4 - ops (this deliverable)
Accept: one command runs the verification pyramid green.
Status: SHIPPED as `netherite_sweep.sh` (repo root). At the 2026-08-07 70%
checkpoint, `--quick` passes 15/15 with no skips. `--full` passes all 26
available steps with zero failures and skips only the undistributed local
canonical tape. A FAIL exits nonzero; SKIPs never do.

## Running the sweep

```bash
bash netherite_sweep.sh --quick          # builds + unit batteries + blaze CPU gate + vec-env (<10 min)
bash netherite_sweep.sh --full           # + blaze core CUDA oracle, blaze env CUDA gate, canonical tape replay (GPU1), raster parity, RL smoke (<40 min)
bash netherite_sweep.sh --full --gpu 0   # device for blaze CUDA steps (tape replay + parity stay pinned to GPU1)
```

Each step wraps an existing gate (make target or script - nothing reimplemented), has
its own timeout and log (path printed at start), and reports [PASS]/[FAIL]/[SKIP] plus
a summary table. GPU steps preflight `nvidia-smi` and SKIP when the device is >50%
util - the box is shared. Missing artifacts (snapshots, tapes, prefixes) SKIP with a
reason. Deeper/slower layers of the pyramid stay where they live: the nightly all-tape
sweep is `verify/nightly_verify.sh`, per-kernel CPU==CUDA verifies are
`make -C blaze verify-<kernel>`.

## Pinned engineering gates (standalone)

These are not the four ship gates above; they are locked harnesses that reject
unintentional drift. Run them after touching the corresponding surfaces.

| Gate | Invoke | What it pins |
|------|--------|--------------|
| Kernel pair parity | `bash scripts/kernel_parity_gate.sh` | CUDA/Metal kernel hashes + cpu==gpu frames |
| Wrapper worldgen census | `bash verify/worldgen/wrapper_gate.sh` | magma product populate wrapper vs blaze `owr` reference under fluid=OFF + shroomlight=stale (seeds 0 7 9 19, origin 2x2) |

### Wrapper worldgen census

Sidecar: `verify/worldgen/known_divergences.json`. Per seed: `diff_cells`,
class breakdown (`fluid` / `mushroom` / `other`), and `diff_sha256` (sha256 of
sorted `x,y,z,blaze_state,magma_state` lines). Counts alone are not enough:
opposite-sign cell changes can cancel.

Blessed 2026-08-02 as KNOWN wrapper mechanics (not bugs): ore-family multi-window
apply (stone -> granite/diorite/andesite) and load-order raster-vs-reverse delta.
Any drift from the sidecar FAILS (rc=1) with a per-seed count/class/hash report.
`--update` rewrites the sidecar; that is maintainer judgment only.

Diagnostic (not the gate): `bash verify/worldgen/wrapper_diff.sh`.

OPEN policy note (not pinned): `MAGMA_FLUID_CA=1` changes 279/10936/4885 cells at
seeds 0/7/9; no gate pins magma default-off vs blaze always-on.

Not wired into `scripts/delegate_gate.sh` / `scripts/regression_pins.txt` (those
files are frozen). Invoke the wrapper gate standalone after populate/wrapper or
owr-path changes.

## Out of scope

- Dragon-fight RL (the sim supports the fight; no RL training on it)
- Multiplayer / servers
- Any Minecraft version other than 1.11.2
- Multi-GPU training or inference (single-GPU pins only)
## Horse-family skeleton-trap render checkpoint (2026-08-16)

- `verify/ui_entities/measure_horse_subject.py` passes its bounded
  fixed-function contract and mutation test with 77 isolated-rider and 2,989
  group pixels above channel delta 25, down from 538 and 3,574.
- `magma/game/test_entity_render.sh` passes with explicit two-sided skeleton
  base and helmet capacity checks.

## Llama dynamic-navigation checkpoint (2026-08-16)

- `magma/trace/test_llama_caravan.py` passes sixteen direct task-boundary
  fixtures,
  the original exact 20-tick obstacle continuation, eight dynamic terrain
  ticks, and a 16-tick follow-parent continuation against a fresh real 1.11.2
  oracle.
- The dynamic slice proves both sides of `PathWorldListener`'s spatial filter:
  a distant collision change keeps the active path, while a nearby wall
  opening replaces it. Materialized path, motion, and on-ground state are
  exact after the edit.
- Follow-parent preserves its child-sized navigator through an airborne tick,
  refreshes it on Java's ten-tick clock, and matches the entity RNG cursor.
  Caravan/ranged, caravan/mating, swimming concurrency, follow-parent
  concurrency, and accepted wander/watch/idle starts are directly locked.
- Two panic scenarios match nearest-water and random-position navigation for
  24 ticks total. One 16-tick mating scenario matches the complete path,
  task mask, RNG cursor, contact state, and motion through an airborne
  entity-target request that retains Java's existing path.
- One 41-tick ranged scenario naturally acquires a wild wolf, traverses a
  forced A* detour, retains its path while airborne, stops after twenty
  visible ticks, and spits at the exact attack-clock boundary. Every path
  point, private clock, RNG cursor, target, contact flag, and motion bit
  matches Java.
- `magma/game/test_llama_runtime` passes with the common horse-family 1.0 step
  height and no horse-only generic jump shortcut.

## Bat active-lifetime checkpoint (2026-08-16)

- `magma/trace/test_bat_ai.py --port 25601` passes 101 bit-exact ticks against
  a real 1.11.2 client across eleven fixtures. The comparison locks position,
  motion, AABB, rotation, body helper, private flight target, age, persistence,
  and the entity RNG cursor, plus both hard and soft despawn outcomes.
- `magma/trace/test_bat_capsule.py --port 25601` passes a nine-tick warmed Java
  checkpoint followed by sixteen exact native ticks. The native twelve-tick
  save fork and the capsule selftest pass independently.
- `verify/completeness/bat_family_gate.py` records Bat as `live_bounded` with
  only natural world-spawner pack eligibility open.
- The CPU performance guard passes at 3,923 steps/s against the frozen 3,858.9
  floor. No GPU path changed.

## Bat render checkpoint (2026-08-16)

- `ENTITY_GATE_BAT_ONLY=1 bash verify/ui_entities/run_oracle_gate.sh` passes
  flying, hanging, and background states captured twice from real Java.
- Both poses have exact native/Java subject ownership: 1,064 pixels flying
  and 852 hanging. Flying has zero pixels above max-channel 25 and a maximum
  channel delta of 1. Hanging has one measured body/tail 24-bit depth-tie
  pixel above 25 and a maximum channel delta of 33.
- `magma/game/test_entity_render.sh`, `magma/game/test_bat_runtime`, and the
  Bat tape-parser regression lock hierarchy, animation, live hanging state,
  and exact `ticksExisted`. The pixel gate includes a subject-erasure negative
  control.
- The broad completeness gate and full `make -C magma test-game` aggregate
  pass. The aggregate finishes in 15:14.08 at 2,347,260 KiB peak RSS with zero
  major faults and zero swap; route-to-credits is green. CPU throughput is
  3,923 steps/s against the unchanged 3,858.9 floor.

## Squid active-lifetime checkpoint (2026-08-16)

- `magma/trace/test_squid_ai.py --port 25601` passes 76 bit-exact ticks against
  real 1.11.2 across five controlled water, dry, vector-refresh, and age
  fixtures. Position, motion, AABB, yaw, all private animation fields, random
  swim vector, persistence, and the entity RNG cursor match at every tick.
- `magma/trace/test_squid_capsule.py --port 25601` passes a nine-tick warmed
  Java checkpoint followed by sixteen exact native ticks. The native
  twelve-tick save fork independently resumes private animation, random
  motion, age, persistence, and RNG without a bit changing.
- `verify/completeness/squid_family_gate.py` records Squid as `live_bounded`;
  only natural world-spawner pack construction and eligibility remain open at
  this promoted family boundary.

## Squid render checkpoint (2026-08-16)

- `ENTITY_GATE_SQUID_ONLY=1 bash verify/ui_entities/run_oracle_gate.sh` passes
  two distinct `RenderSquid` poses plus a same-scene background, each captured
  twice from real Java with zero A/B noise.
- Native now consumes the exact live `renderYawOffset`, `squidPitch`,
  `squidYaw`, and `tentacleAngle` fields. The dry pose has 6,889 owned pixels,
  zero pixels above max-channel 25, and maximum channel delta 1. The swimming
  pose has 18,642 owned pixels and one measured internal-tentacle depth-tie
  pixel above 25, under an explicit one-pixel budget; maximum delta is 32.
- The renderer gate includes a hard-pixel negative control. The model test
  locks all nine cull-disabled boxes, UV ownership, whole-body rotations, and
  live tentacle hinges; the runtime test locks the simulation-to-render
  handoff bit-for-bit.
- The broad completeness gate and full `make -C magma test-game` aggregate
  pass. The aggregate reaches route-to-credits in 16:11.06 at 2,347,268 KiB
  peak RSS, one major fault, and zero swap. `test-game` now names the focused
  Squid runtime explicitly; that binary and the route-to-credits tail pass
  again after the integration change. CPU throughput is 3,945 steps/s against
  the unchanged 3,858.9 floor.

## Snow Golem live-bounded checkpoint (2026-08-17)

- Three direct 1.11.2 shearing cases match pumpkin state, tool durability,
  Unbreaking RNG, entity/item IDs, drops, and events exactly.
- Three direct ranged-launch cases match owner, position, heading, rotations,
  entity/server UUID RNG, entity ID, and shoot sound state bit-for-bit. The
  fifteen-case snowball-impact matrix includes five Snow Golem-owned zombie
  and Blaze branches with exact damage, immunity, and revenge-owner identity.
- The complete 345-row represented-hostile loot gate includes fifteen Snow
  Golem rows and matches the zero-to-fifteen snowball table and RNG cursor.
  Native runtime locks the first shot at tick 21 and exact continuation across
  both a pending shear packet and an in-flight owned snowball.
- `verify/completeness/snowman_family_gate.py` promotes EntitySnowman to
  `live_bounded`, bringing the registry to 63 bounded and 18 partial entities.
  Construction spawning, general task/path continuation, hostile retaliation
  consumption, environmental cross-stack ticks, and same-scene model pixels
  remain explicit.
- Focused runtime, entity-model, jar-atlas, registry, and oracle gates pass.
  CPU throughput passes at 3,890 steps/s against the frozen 3,858.9 floor with
  the unchanged trajectory hash. No GPU path changed.

## Endermite live-bounded checkpoint (2026-08-17)

- Five direct Ender Pearl impact cases lock the 5 percent spawn branch,
  player-spawned bit, lifetime zero, position/motion/rotation, entity and UUID
  allocation, and entity/world RNG state against real 1.11.2.
- Fifteen direct loot rows lock Endermite's empty table inside the complete
  345-row hostile aggregate. Nine composed lethal-hit rows are part of the
  243-row Java/native death matrix, and the native terminal regression locks
  three split XP, retirement order, constructor state, and twenty death
  particles.
- A 20-tick real Java/NBT/capsule/native continuation retains lifetime,
  player-spawned and persistence state. The focused native save test locks
  the 2399/2400 expiration boundary and the persistent branch.
- Exact inherited hostile fall handling, ambient/hurt/death audio resources,
  the jar Endermite texture, four-box animated model, 0.4 by 0.3 dimensions,
  0.1 eye height, and 180-degree death keel are implemented and gated.
- `verify/completeness/endermite_family_gate.py` promotes EntityEndermite to
  `live_bounded`, bringing the registry to 64 bounded and 17 partial entities.
  Natural pack spawning, Enderman retaliation, exact active task/path
  continuation, step-event production, portal particles, and same-scene
  pixels remain explicit.
- The clean broad completeness gate passes after making its `magma_game`
  prerequisite explicit. The audio regression now keeps its three large
  runtime fixtures out of the 8 MiB process stack. CPU throughput passes at
  4,089 steps/s against the frozen 3,858.9 floor; no GPU path changed.

## Spectral Arrow live-bounded checkpoint (2026-08-21)

- Eight direct 1.11.2 payload cases pass, including configurable spectral
  duration and exact Glowing application. The native runtime independently
  locks bow/Infinity consumption, mob and player impacts, pickup item/status,
  render-state handoff, and checkpoint continuation.
- A current real-Java state containing normal, tipped, and spectral arrows
  crosses the neutral capsule and continues for one native tick with exact
  position, motion, rotation, collision state, subclass payload, pickup data,
  UUID, RNG, semantic stack NBT, and loaded entity identity.
- `verify/completeness/spectral_arrow_family_gate.py` promotes
  EntitySpectralArrow to `live_bounded`, bringing the registry to 65 bounded
  and 16 partial entities. Arbitrary mixed-entity collision ordering,
  foreign-dimension terminal delivery, and strict projectile pixels remain
  explicit under `ENT-06`.

## Giant live-bounded checkpoint (2026-08-21)

- The 1.11.2 class installs no AI or target tasks. The live simulator now
  preserves that unusual contract: an active Giant still receives living
  physics, collision, despawn, damage, and death updates, but never acquires
  a player, attacks, navigates, or enters the generic wander path.
- Fifteen exact empty-loot rows are part of the represented hostile matrix of
  345. Nine composed lethal player hits are part of the death matrix of 243.
  The native
  terminal suite covers exact five-XP splitting, entity order, retirement,
  and twenty particles for Giant as its twenty-first family.
- A focused active runtime holds the goal-less state for 40 ticks and proves
  byte-identical continuation across a tick-20 native save/reload boundary.
  The existing 34-class real-Java NBT capsule separately covers Giant's
  represented living state for 20 ticks.
- The entity renderer now explicitly gates the zombie model and jar texture
  at uniform scale six, exact 3.6 by 11.7 dimensions, and 10.440001 eye
  height. The registry is now 66 bounded and 15 partial entities.

## Chest and Furnace Minecart live-bounded checkpoint (2026-08-21)

- The direct 1.11.2 minecart golden passes all ten rail shapes plus straight,
  powered, braking, slope, derailed, collision, rider, activator, detector,
  damage, and destruction branches. Furnace-specific speed cap, push, fuel,
  smoke RNG, and playable fueling match bit-for-bit; chest inventory and drop
  paths are included.
- The direct Structure oracle preserves all seven concrete cart classes and
  their base/display state. Chest inventory and Furnace fuel/push state retain
  exact constructor RNG and fresh UUID behavior. The capsule/checkpoint path
  preserves arbitrary tagged chest slots and loaded-entity order.
- Concrete render views and models are gated for both classes. Furnace is at
  the strict channel-one pixel floor; Chest retains only bounded classified
  fixed-function shading ties. Their registry rows are now `live_bounded`,
  bringing the entity ledger to 68 bounded and 13 partial.
- Spawner and Command Minecarts remain partial because arbitrary entity-tag
  spawn families and general command dispatch/result statistics are still
  major product surfaces.

## Husk live-bounded checkpoint (2026-08-21)

- A parked real 1.11.2 server now directly proves empty-hand Husk melee at
  two Normal-difficulty local-difficulty boundaries: 140 ticks of Hunger in
  a fresh world and 280 after the world-age/moon integer-cast transition.
  Native ticks reproduce the delayed first exhaustion pulse and exact 0.005
  exhaustion per active tick.
- A focused runtime proves exposed daylight immunity against simultaneously
  igniting Zombie and Stray controls, then forks an active Hunger effect
  through native save/reload with exact duration and exhaustion continuation.
- Husk participates in all 345 exact hostile-loot rows, all 243 composed
  death rows, and the 23-family native death/XP/particle terminal suite. Its
  zombie model, jar-exact skin, and four owned sound assets are gated.
- The checked family manifest promotes EntityHusk to `live_bounded`, bringing
  the entity ledger to 69 bounded and 12 partial. Chunk inhabited-time local
  difficulty, natural desert pack creation, full reinforcement/equipment/
  door behavior, type-specific step emission, and strict model pixels remain
  explicit bounded residuals.

## Stray live-bounded checkpoint (2026-08-21)

- A parked real 1.11.2 server directly invokes `EntityStray.getArrow` and
  proves an `EntityTippedArrow` with one visible, non-ambient Slowness-I custom
  effect at duration 600. The native active ranged path emits the same payload
  on its inherited 40-tick Skeleton cadence and applies it on player impact.
- Fifteen exact Stray loot rows include both common arrow/bone pools and the
  killed-by-player tipped-arrow pool with canonical
  `{Potion:"minecraft:slowness"}` NBT. Nine composed death rows and the native
  terminal suite lock five XP, retirement, item order, and twenty particles.
- A focused runtime save fork preserves the in-flight projectile byte-exactly
  and separately restores the tagged tipped-arrow loot NBT. The 345-row loot,
  243-row death, and 23-family native terminal aggregates remain green.
- The checked family manifest promotes EntityStray to `live_bounded`, bringing
  the entity ledger to 70 bounded and 11 partial. Natural snowy-biome spawn
  replacement, full difficulty-scaled equipment/enchantment, exact step-event
  production, arbitrary projectile collision ordering, and strict same-scene
  pixels remain explicit bounded residuals.

## Rabbit, Polar Bear, and complete entity-ledger checkpoint (2026-08-22)

- Rabbit passes its direct real 1.11.2 combat, jump-threshold, and NBT probe.
  Its focused runtime covers Killer Bunny targeting and exact eight damage,
  ten-tick hopping, mature-carrot raid and cooldown, plus byte-exact native
  save/reload continuation. The twelve-box model, eight coat textures, child
  pose, five sound events, breeding food, and type-99 armor are gated.
- Rabbit adds fifteen exact loot rows and nine exact composed death rows. The
  current aggregates pass 375 loot rows, 261 deaths, and 25 native terminal
  families with 500 death particles. Polar Bear retains its independently
  checked child-defense, warning-melee, loot/death, continuation, model, and
  audio boundary against those expanded aggregates.
- The direct Minecart Java/native comparison covers spawner activation,
  countdown/reset RNG, and command-cart activator cooldown, payload, built-in
  `Searge` result, and checkpoint state. Together with the existing Structure,
  capsule, motion, collision, destruction, and render proofs, all seven cart
  variants are now independently `live_bounded`.
- The registry gate now reports all 81 registered entities as `live_bounded`.
  This is entity-registry closure, not whole-product closure: tile, world,
  command, UI, audio, generalized topology, and strict pixel TODOs remain in
  the completeness ledger.

## Complete tile-ledger checkpoint (2026-08-22)

- Rich four-line sign JSON and named two-pattern banner NBT survive real Java
  capture, state-capsule restore, support-loss retirement, exact drops, and
  native checkpoints. Both focused Java/native cases match all 30 state
  features, raw blocks, and block light.
- Skull tiles preserve all six types, sixteen rotations, and signed player
  profiles. Dragon and player skull piston-destruction cases match Java drop
  payloads and RNG exactly.
- Command-block tiles preserve all three variants and exact loaded order. The
  bounded vanilla `Searge` execution emits the exact JSON output, success one,
  and comparator update. The complete bounded vanilla `time set` family
  (`day`, `night`, and nonnegative int32 values), `time add`, and all three
  `time query` forms now match Java's world-time effects or non-mutation and
  timestamped translation JSON grammar. All seven focused time cases and a
  mixed command/sign retirement case pass all state,
  behavior, block,
  and light gates; native checkpoints retain command text, output, and effect.
  Explicit-duration `weather clear`, `rain`, and `thunder` add three more
  strict cases, including exact persisted flags, timers, visual-strength ramp,
  output, and cold-capsule continuation. This found and fixed an exporter bug
  where a strength-threshold view was mislabeled as the persisted rain flag.
  All eleven cases now fork from the same unexecuted pre-tick command state,
  replay the trigger into Java and native independently, and pass Java's
  captured wall-clock fields as an explicit external input. The stricter fork
  also found and fixed empty-output normalization and native private-clock
  synchronization for `time set/add`.
  The complete 11-case same-input family passes at
  `/home/jawaugh/dev/nw/.tmp/deferred_command_family/summary.md`; a final
  post-regression `time add` rerun passes at
  `/home/jawaugh/dev/nw/.tmp/deferred_command_time_add_post_rabbit/summary.md`.
  The focused runtime aggregate passes in 6:20.98 at 524,096 KiB peak RSS,
  zero major faults, and zero swap. `test_world_live.sh` passes at 1,092,280
  KiB peak RSS, and the scalar CPU guard passes at 4,105 steps/s against the
  4,062 baseline and 3,858.9 floor. The quick pyramid's only non-pass was its
  default connection to reserved oracle port 25600; rerunning the exact
  completeness lane on permitted instance 1 (`QRL_PORT=25601`) passes in
  1:08.65 at 774,608 KiB peak RSS. The two undistributed `.bsnp` stages remain
  the only skips.
  Default-duration `weather clear`, `rain`, and `thunder` add three same-input
  cases at `/home/jawaugh/dev/nw/.tmp/weather_default_strict_v2/summary.md`.
  The constructor-private Java Random outcome is modeled as an explicit
  duration input, while ordinary native play retains vanilla's 300-899 second
  range. All three cases pass 30/30 state families, behavior, raw blocks, and
  block light.
  The post-promotion monolithic runtime passes in 5:07.39 at 519,812 KiB peak
  RSS with zero major faults and zero swap. The complete local ledger passes
  in 1:10.72 at 774,416 KiB peak RSS, and scalar throughput passes at 4,971
  steps/s against the 4,062 baseline and 3,858.9 floor.
  After that correction, the full native runtime aggregate passes in 5:36.12
  at 523,804 KiB peak RSS with one major fault and zero swap; the scalar guard
  passes at 4,870 steps/s against the 4,062 baseline and 3,858.9 floor.
  The post-promotion quick pyramid passes in 2:36.00 at 2,350,304 KiB peak RSS
  with zero swap; only the two undistributed `.bsnp` stages skip.
  The checkpoint-enabled monolithic runtime passes in 5:01.95 at 591,952 KiB
  peak RSS with zero major faults and zero swap. The stopped-oracle CPU guard
  remains green at 5,014 steps/s.
- The registry reports 81/81 entities and 24/24 tile entities as
  `live_bounded`. The native runtime aggregate passes in 6:42.90 at 515,636
  KiB peak RSS with zero major faults and zero swap. CPU throughput passes at
  5,031 steps/s against the 4,062 baseline and 3,858.9 floor. GPU 1 was not
  executed. General commands, arbitrary cross-feature ordering, strict tile
  pixels, and the broader completeness ledger remain open.
- The final quick pyramid passes every locally runnable stage in 2:38.63 at
  2,350,260 KiB peak RSS with zero swap. The only skips are the two existing
  `.bsnp` stages whose snapshots are not distributed locally. During the
  sweep, the Firework Java golden label was synchronized with its already
  checked main/offhand priority coverage. An idle villager RNG fixture was
  moved from generated stone at Y=65 to verified air at Y=200; this removes
  exact suffocation damage and its measured four-draw RNG effects at ticks 0
  and 10. The focused village runtime and the full parity-60 aggregate pass.

## 62-row bounded closure checkpoint (2026-08-22)

- The run queue is 62/62 complete with zero queued and no duplicate IDs. This
  is bounded owner closure, not a claim that the 92-row master ledger is all
  strict `DONE`: the strict audit remains 23/92 and every residual tail stays
  named in `docs/COMPLETENESS.md` or `magma/OPEN_DIVERGENCES.md`.
- The final quick sweep is fully green with no skips in 3:37.65 at 2,350,236
  KiB peak RSS and zero swap. Its completeness aggregate passes in 132
  seconds, the 60-tick parity lane in 43 seconds, and the two selected
  real/native Blaze streams match for 1,000/1,000 ticks.
- Frozen performance guards pass at 4,237 CPU scalar steps/s, 2.84M
  full-feature batched CUDA env-ticks/s, and 66.36 CUDA fps at 1080p. Their
  floors are 3,858.9, 2.793M, and 24.29 respectively; the render result also
  clears the separate 60 fps ship target.
- CPU/CUDA raster is bit-exact across solid, cutout, translucent, D24 bias,
  mip, and fog cases. The 2,048-lane mixed full-action trajectory is bit-exact
  on all 64 sampled CPU lanes, and the 64-lane 2,058-action chain is bit-exact
  every tick. The tracked Metal helper hash predates this run and is stale;
  it is not re-recorded without the required Mac numeric proof.
- The uninterrupted native runtime battery passes in 6:40.21 at 527,680 KiB
  peak RSS, zero major faults, and zero swap. A freshly baked 13-seed
  curriculum census exposed known Java/native liquid and item-pickup tails;
  those failures are retained in `magma/OPEN_DIVERGENCES.md` rather than
  weakened into passes.
