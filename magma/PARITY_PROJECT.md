# Minecraft 1.11.2 full-parity project

Status date: 2026-08-07

This is the ordered execution board for expanding Netherite from its current
speedrun/RL product contract toward a full single-player Minecraft Java 1.11.2
replica. The user's full-parity request promotes the features called "cuts" in
`PRODUCT.md` into backlog work. They remain disabled in the fast base profile
until their complete bundle is implemented and gated.

`OPEN_DIVERGENCES.md` remains the detailed evidence log. This file decides what
we do next, which test proves it, and which performance budget protects it.

## Rules of execution

Every parity item follows the same loop:

1. Construct the smallest deterministic Java fixture that exhibits one
   behavior. Save the complete fixture inputs and pre-tick state.
2. Run Java and magma from the same state and input tape.
3. Reject contaminated runs before diagnosis: exact client-tick increments,
   verified fixture read-back, stable starting pose, and non-vacuous mutations
   where a mutation is expected.
4. Fix the earliest causal divergence, not a later symptom.
5. Add a narrow positive regression and at least one negative control that
   proves the comparator can fail.
6. Run the aggregate state/block/pixel gates.
7. Run the performance guard. A feature that is off must not add work to the
   base profile; enabled features must use bounded, dirty-driven work.

An item is `DONE` only when its Java repro, narrow gate, aggregate gate, and
performance result are committed together. A behavior with a `null` C field is
unobserved, not passing.

## Current known-good baseline

The exact-step full-runtime oracle matrix is the first mandatory end-to-end
simulation gate. The current-source correctness promotion covers 676
composite behavior and exact raw-block outcomes. The four-client aggregate ran all 549
cases in 2,751.851 seconds at
`trace/out/matrix_redstone_piston_farmland_grass_path_shape_full_1/summary.md`.
It recorded 543 ordinary passes, four expected diagnostics, and two
fixture-contaminated older rows. Case-local corrections for those two rows and
the four new rows pass 6/6 at
`trace/out/redstone_piston_farmland_grass_path_shape_fixture_hardening_1/summary.md`,
so the composite promotion has 545 strict full-state passes and four
third-tick cactus settlement rows whose
randomized item trajectories remain diagnostic because Java resumes ambient
loaded-chunk RNG and entity-ID work after the controlled input boundary. Their
start boundary, raw volume, item type/count, and age are strict and pass.
The 99 subsequent entity-trigger and redstone rows pass independently on
the single-client lane. Thirty-four later tripwire, collision-geometry, ordinary
player-physics, and potion rows also pass independently. Every strict row has 25 matching
state features, or 26 under the current max-health/absorption schema, zero
divergences, one explicit
unsupported subfield (`death_time`), and exact raw block transitions. Current
performance evidence passes at 4,775 scalar steps/s, 2.93M Blaze env-ticks/s,
and 30.67 CUDA fps on GPU 1 at
`trace/out/perf_guard_ordinary_cauldron_hopper_flower_pot_cactus.json`.
All three metrics remain above their machine-local regression floors. The
latest live-blaze slice
adds no work to the inactive-projectile path. An active small fireball checks
only the bounded cells crossed by its sub-two-block tick segment; shaped-box
and neighbor work runs only when one of those cells is occupied, then scans the
fixed 95-slot represented mob/boat store without allocation.
The exact entity-RNG slice adds fixed per-slot Java Gaussian state and no heap
work. Java, CPU, and CUDA agree on all 17 raw-bit/state outputs for blaze aim,
fireball acceleration, cached Gaussian reuse, and floating motion. A real
eight-tick `EntitySmallFireball` save-state fixture passes the full-runtime
entity comparison at
`trace/out/small_fireball_trajectory_regression`: position, velocity, and
acceleration are exact on every tick, with 10,625/10,625 final block cells
unchanged and exact.
In the End, the same active-projectile branch checks the represented dragon's
eight parts and ten crystals. That 18-candidate scan is fixed, allocation-free,
and absent from the inactive projectile path.
The final-source native aggregate also passes under elevated host load in 5:45
with a 288 MiB peak and
zero swap at
`trace/out/test_runtime_ordinary_cactus_collision_damage.log`.
The latest exact-source aggregate through physical jukebox ejection passes in
5:15.70 with a 296,108 KB peak and zero swap at
`trace/out/test_runtime_daylight_periodic_cake_flower_pot_jukebox_ejection_final.log`.

The latest R-04 promotion extends ordinary-player collision beyond redstone
diodes with exact brewing-stand stem/base, enchanting-table, farmland,
grass-path, single-slab, carpet, snow-layer, and cake boxes. Deliberate
old-magma fixtures fail at the first landing coordinate while retaining exact
raw blocks and block light. The corrected 12-case affected family passes at
`trace/out/matrix_ordinary_player_thin_surface_shapes_affected_1/summary.md`;
brewing remains diagnostic only for its already-declared unsupported inventory
tile and the other eleven rows are strict. Focused native coverage exhausts
the represented metadata states, and CPU and CUDA agree on all 2,432 emitted
player-survival values. The full aggregate and clean performance guard pass at
the paths and rates above.

The latest ordinary-player promotion adds beds, both daylight detectors,
end portal frames, ender chests, both trapdoors, and actual-state chorus plants
now use their exact Java collision boxes in the shared CPU/CUDA movement path.
Six deliberate old-magma jump fixtures fail only player physics while retaining
exact raw blocks and block light; all corrected rows are strict with 25 matching
features at
`trace/out/matrix_ordinary_player_bed_daylight_frame_ender_chest_candidate_1/summary.md`
and `trace/out/matrix_ordinary_player_trapdoor_chorus_candidate_1/summary.md`.
The 12-case affected family passes at
`trace/out/matrix_ordinary_player_bed_daylight_frame_ender_chest_trapdoor_chorus_affected_1/summary.md`;
CPU/CUDA agree on 2,432 outputs, and the full native suite passes under elevated
host load in 6:24 with a 288 MiB peak and zero swap at
`trace/out/test_runtime_ordinary_bed_daylight_frame_ender_chest_trapdoor_chorus.log`.
After the unrelated 56-thread stage ended, the clean GPU 1 guard passed at
4,765 scalar steps/s, 2.93M Blaze env-ticks/s, and 30.50 CUDA fps at
`trace/out/perf_guard_ordinary_bed_daylight_frame_ender_chest_trapdoor_chorus.json`.
These six strict outcomes bring the promoted total to 671.

The next promotion adds exact ordinary-player collision for
cauldrons, hoppers, and flower pots. It also fixes the earlier causal state
gap: live placement now materializes empty hopper and flower-pot tiles before
the next observation. The deliberate old-C behavior probe fails on all three
landing paths, and the hopper/pot probe first fails at tile creation; after the
tile fix that divergence moves to player physics, and after the shape fix all
three corrected rows are strict at
`trace/out/matrix_ordinary_player_cauldron_hopper_flower_pot_candidate_1/summary.md`.
The nine-case affected family passes at
`trace/out/matrix_ordinary_player_cauldron_hopper_flower_pot_affected_1/summary.md`,
CPU/CUDA agree on all 2,432 player-survival outputs. The clean combined
performance guard passes at the current rates and path above, promoting all
three strict rows and bringing the total to 674.

Cactus 81 now contributes its exact 1/16 horizontal inset and 15/16 height to
ordinary-player collision, and its contact callback enters the authoritative
one-point damage, armor, hurt-immunity, and 0.1-exhaustion path. The deliberate
old-C jump probe first fails cactus damage at tick 1 and later lands on the
old full cube. A causal damage-only rerun makes tick-1 health, hurt time, and
exhaustion exact before the remaining geometry failure. The corrected
stationary lifecycle is strict, including damage at ticks 1/11/21 and natural
regeneration at 10/20; the jump row has exact raw/light state and measured
inset landing/contact but remains diagnostic because Java's self-velocity
packet arrived at tick 3 and tick 4 in consecutive oracle runs. Both proofs
pass in the 12-case affected family at
`trace/out/matrix_ordinary_player_cactus_affected_1/summary.md`. Native tests
exhaust all 16 metadata states, CPU/CUDA agree on 2,432 outputs, the final
native aggregate passes at the current path above, and the clean performance
guard passes at 4,775 scalar steps/s, 2.93M Blaze env-ticks/s, and 30.67 CUDA
fps. These one strict and one bounded-diagnostic outcomes bring the promoted
total to 676.

Five further strict R-04 outcomes are implementation-complete candidates.
Horizontal end rods and all six skull facings now use their exact boxes in the
shared player path, and live skull placement creates the default type-0,
rotation-0 tile before the next observation. Lily pads use their inset
3/32-high box; ordinary and trapped chests use same-registry joined 7/8 boxes,
and live chest placement creates the empty 27-slot tile. The old-C probes fail
at the first tile or landing boundary with exact blocks and light. The causal
tile reruns move the first chest/skull divergence to physics, and all five
final rows are strict at
`trace/out/matrix_ordinary_player_end_rod_skull_candidate_1/summary.md` and
`trace/out/matrix_ordinary_player_lily_chests_candidate_2/summary.md`. The
18-case affected family passes at
`trace/out/matrix_ordinary_player_lily_chests_affected_1/summary.md`; CPU and
CUDA agree on 2,432 outputs, and the exact-source native suite passes in 6:27
with a 289 MiB peak and zero swap at
`trace/out/test_runtime_ordinary_lily_chests_final.log`. A clean performance
recapture is deferred while GPU 1 is shared, so these rows do not yet change
the promoted total of 676.

A sixth strict R-04 candidate adds actual-state ordinary-player collision for
all 14 stair IDs. The bounded forward/brake proof places the complete
0.6-wide player box over the low half: Java reaches Y 78.5 at tick 9 and
grounds at tick 10, while old magma lands on its full cube at Y 79. The
corrected row is strict at
`trace/out/matrix_ordinary_player_stair_candidate_1/summary.md`, and all 15
affected stair/piston/pane/support/player rows pass at
`trace/out/matrix_ordinary_player_stair_affected_1/summary.md`. Native coverage
checks every ID, top/bottom half, facing, and straight/inner/outer shape;
CPU/CUDA agree on 2,432 outputs, and the native suite passes in 5:09 with a
289 MiB peak and zero swap at
`trace/out/test_runtime_ordinary_stairs.log`. GPU 1 remains fully occupied by
the shared process, so this row joins the performance-pending candidates and
the promoted total remains 676.

A seventh strict R-04 candidate adds actual-state pane collision to the shared
ordinary-player path for iron bars, glass panes, and stained panes. The
discriminating north approach isolates the center post: Java reaches
Z 7.862500011920929 and stops on tick 4, while old magma's full-cube fallback
stops at Z 8.300000011920929 on tick 1. The corrected approach and centered
landing pass at
`trace/out/matrix_ordinary_player_glass_pane_candidate_1/summary.md`; all 14
affected pane/piston/stair/player-trigger rows pass at
`trace/out/matrix_ordinary_player_glass_pane_affected_1/summary.md`. Native
coverage checks all three IDs, all metadata, every arm, Forge side-solid
exceptions, and actual-state stair sides. CPU/CUDA agree on 2,432 outputs, and
the native suite passes in 4:41 with a 289 MiB peak and zero swap at
`trace/out/test_runtime_ordinary_panes.log`. Performance remains deferred on
the shared GPU, so the promoted total remains 676.

Three further strict R-04 candidates add ordinary-player collision for
retracted and six-facing extended piston bases, both anvil axes, and dragon
eggs. Pre-staged save states remove block-update packet timing from the proof:
old magma stops all three at the full-cube Z 8.300000011920929 on tick 1,
while Java reaches the exact faces at Z 8.050000011920929,
8.175000011920929, and 8.237500011920929. The corrected rows pass at
`trace/out/matrix_ordinary_player_piston_base_anvil_dragon_egg_candidate_static_2/summary.md`,
and all 16 affected player/piston/mobility rows pass at
`trace/out/matrix_ordinary_player_piston_base_anvil_dragon_egg_affected_1/summary.md`.
CPU/CUDA agree on 2,432 outputs, and the native suite passes in 4:52 with a
289 MiB peak and zero swap at
`trace/out/test_runtime_ordinary_piston_base_anvil_dragon_egg.log`. These rows
also await the clean performance recapture, so the promoted total remains 676.

Three further strict R-04 candidates add all seven 1.11.2 fence IDs and all
six fence-gate IDs to ordinary-player movement. Closed gates retain their
axis-specific 1.5-high panel, open gates have no collision, and fences use the
exact post/arm actual state with wood-vs-nether partitioning and opaque-neighbor
exceptions. Old magma treats the closed gate, open gate, and spruce fence as
full cubes at Z 8.300000011920929; the corrected rows pass at
`trace/out/matrix_ordinary_player_fence_gate_fence_candidate_1/summary.md`.
All 16 affected player/piston/wall/support rows pass at
`trace/out/matrix_ordinary_player_fence_gate_fence_affected_1/summary.md`;
CPU/CUDA agree on 2,432 outputs, and the native suite passes in 4:41 with a
289 MiB peak and zero swap at
`trace/out/test_runtime_ordinary_fences_gates.log`. These rows remain
performance-pending and the promoted total remains 676.

Four further strict R-04 candidates add ordinary-player collision for all
seven door IDs, ladders, and cocoa pods, plus the missing vanilla ladder
travel branch. Door lower/upper pairs combine facing, open, and hinge state
into the exact 3/16 panel; cocoa uses age/facing pod geometry. Ladder/vine
cells clamp horizontal motion, reset fall distance, and convert a horizontal
collision into the 0.2 climb impulse; matching open trapdoors above ladders
also retain ladder identity. Old magma stops every staged approach at the
full-cube Z 8.300000011920929 on tick 1. The corrected closed door reaches
Z 7.487500011920929 on tick 5, the open-door lane stays clear, cocoa reaches
Z 7.862500011920929 on tick 4, and the ladder reproduces the exact 20-tick
clamp/climb/release trajectory. All four strict rows and the 14 neighboring
controls pass in the 18-case affected family at
`trace/out/matrix_ordinary_player_doors_ladder_cocoa_affected_1/summary.md`.
Native tests exhaust door pairs, ladder/cocoa metadata, ladder identity, and
the exact climb trace; CPU/CUDA agree on 2,432 outputs, and the aggregate
passes in 4:25 with a 289 MiB peak and zero swap at
`trace/out/test_runtime_ordinary_doors_ladder_cocoa.log`. These rows await a
clean performance recapture, so the promoted total remains 676.

One further strict ordinary-physics candidate implements MobEffects.LEVITATION
in the shared travel tail. While active, gravity is replaced by convergence
toward `0.05 * (amplifier + 1)` at 0.2 per tick, followed by vanilla's 0.98
vertical drag; expiry restores ordinary gravity before same-tick travel. The
old-C row remains grounded while Java rises, first diverging at tick 0 with
exact potion duration, blocks, and light at
`trace/out/matrix_ordinary_player_levitation_probe_1/summary.md`. The corrected
20-tick rise, expiry, fall, and tick-12 landing pass strict at
`trace/out/matrix_ordinary_player_levitation_candidate_2/summary.md`; all seven
affected movement/liquid/potion/ladder rows pass at
`trace/out/matrix_ordinary_player_levitation_affected_1/summary.md`. A new
focused native effect gate runs before the long aggregate; active-branch
CPU/CUDA agree on 2,432 outputs, and the final aggregate passes with a 289 MiB
peak and zero swap at `trace/out/test_runtime_ordinary_levitation_final.log`.
Host contention invalidates its wall time and GPU 1 is shared, so this row is
performance-pending and the promoted total remains 676.

One further strict ordinary-physics candidate implements MobEffects.JUMP_BOOST
in both client travel and integrated-server movement prediction. The old-C
fixture preserves the amplifier/duration but uses the basic 0.42 jump and
lands at tick 11; Java's amplifier-1 impulse is 0.62, reaches
Y 80.51679398321414, touches the surface at tick 15, and reports authoritative
ground at tick 16. The old result is retained at
`trace/out/matrix_ordinary_player_jump_boost_probe_1/summary.md`; the exact
30-tick arc and potion lifecycle pass at
`trace/out/matrix_ordinary_player_jump_boost_candidate_4/summary.md`. Both
random controls, speed-potion expiry, levitation, cake landing, and ladder
travel pass with it in the 7/7 affected family at
`trace/out/matrix_ordinary_player_jump_boost_affected_1/summary.md`. The fast
native effect gate covers both client and server impulses, and active-branch
CPU/CUDA agree on 2,432 outputs. The same effect also subtracts
`amplifier + 1` from fall damage before the vanilla ceil; the direct
Java/CPU/CUDA vitals gate agrees on all 400 rows, and a native nine-block drop
distinguishes four boosted damage from six ordinary damage. GPU timing remains
excluded while GPU 1 is shared, so this row is performance-pending and the
promoted total remains 676.

One further strict potion candidate implements MobEffects.WATER_BREATHING for
the submerged air-supply path. Old magma decrements air every tick despite the
active effect, first diverging at tick 0 at
`trace/out/matrix_ordinary_player_water_breathing_probe_1/summary.md`. The
corrected eight-tick trace holds the captured air value through duration 2,
duration 1, and the expiry tick, then resumes one-point-per-tick loss at
`trace/out/matrix_ordinary_player_water_breathing_candidate_1/summary.md`.
Both random controls, the full 340-tick drowning lifecycle, surface reset,
Speed expiry, and Levitation pass with it in the 7/7 affected family at
`trace/out/matrix_ordinary_player_water_breathing_affected_1/summary.md`. The
bounded potion scan runs only while the player eye is underwater, and the
fail-fast native effect gate covers expiry. This row is performance-pending
while GPU 1 is shared. The combined final aggregate passes with a 296,036 KB
peak and zero swap at
`trace/out/test_runtime_ordinary_jump_boost_water_breathing_final.log`; its
host-contaminated wall time is excluded. The promoted total remains 676.

One further strict potion candidate implements MobEffects.FIRE_RESISTANCE for
the ordinary-player burn counter and fire-block contact paths. Old magma keeps
the three-tick potion lifecycle exact but accepts the scheduled burn at the
expiry tick, first diverging in health, exhaustion, and hurt state at tick 2 at
`trace/out/matrix_ordinary_player_fire_resistance_probe_1/summary.md`. Java
suppresses that hit because damage is evaluated before duration aging, then
accepts the next scheduled hit at tick 22 after the effect has expired. The
exact 25-tick trace passes at
`trace/out/matrix_ordinary_player_fire_resistance_candidate_1/summary.md`.
Both random controls, the existing fire counter/contact/extinguish rows, and
Water Breathing pass with it in the 7/7 affected family at
`trace/out/matrix_ordinary_player_fire_resistance_affected_1/summary.md`.
The fail-fast native effect gate covers the protected expiry hit and the first
unprotected later hit. Potion lookup occurs only at a scheduled burn-damage or
fire-contact boundary. This twenty-first strict row is performance-pending
while GPU 1 is shared, and the promoted total remains 676.

Two further strict potion candidates implement the ordinary-player periodic
actions for MobEffects.HUNGER and MobEffects.POISON. The old Hunger II row
ages its three-tick effect exactly but leaves exhaustion unchanged, first
diverging at tick 0 at
`trace/out/matrix_ordinary_player_hunger_probe_1/summary.md`. The corrected
six-tick trace adds the amplifier-1 float increment of 0.01 through the expiry
tick and then stops at
`trace/out/matrix_ordinary_player_hunger_candidate_1/summary.md`; all eight
neighboring random, potion, and jump-exhaustion rows pass at
`trace/out/matrix_ordinary_player_hunger_affected_1/summary.md`.

The old Poison II row keeps duration exact but omits its duration-12 magic hit,
first diverging in health and hurt state at
`trace/out/matrix_ordinary_player_poison_probe_2/summary.md`. The corrected
four-tick cadence trace applies one point at tick 0, starts hurt time at 10,
and preserves the one-health floor at
`trace/out/matrix_ordinary_player_poison_candidate_1/summary.md`. The random,
drowning, melee, fire, Speed, Fire Resistance, and Hunger family passes 10/10
at `trace/out/matrix_ordinary_player_poison_affected_1/summary.md`. Both
actions run only inside the already-bounded active-potion aging loop. The
combined Fire Resistance, Hunger, and Poison aggregate passes with a 295,928
KB peak and zero swap at
`trace/out/test_runtime_ordinary_fire_resistance_hunger_poison_final.log`;
its shared-host wall time is excluded. These twenty-second and twenty-third
strict rows are performance-pending while GPU 1 is shared, and the promoted
total remains 676.

One further strict potion candidate implements MobEffects.REGENERATION's
periodic heal. A duration-52 Regeneration I fixture overlaps the ordinary
burn counter so the duration-50 action must restore one scheduled damage point
on tick 2. Old magma preserves fire, hurt state, duration, blocks, and light
but remains at health 19, first diverging only in health at
`trace/out/matrix_ordinary_player_regeneration_probe_1/summary.md`. The exact
five-tick burn-plus-heal trace passes at
`trace/out/matrix_ordinary_player_regeneration_candidate_1/summary.md`; both
random controls, drowning, fire counter/contact, Fire Resistance, Hunger, and
Poison pass with it in the 9/9 affected family at
`trace/out/matrix_ordinary_player_regeneration_affected_1/summary.md`. The
fail-fast native gate covers the same cadence and same-tick food-timer reset.
The action runs inside the existing bounded potion loop.

One further strict potion candidate implements MobEffects.WITHER's periodic
damage for directly applied effects. Old magma ages Wither II from duration 20
but omits its duration-20 damage and hurt state, first diverging at tick 0 at
`trace/out/matrix_ordinary_player_wither_probe_1/summary.md`. The corrected
four-tick trace holds health at 19, ages hurt time 10 through 7, and ages the
effect 19 through 16 at
`trace/out/matrix_ordinary_player_wither_candidate_1/summary.md`. Both random
controls, drowning, melee, fire, Hunger, Poison, and Regeneration pass with it
in the 10/10 affected family at
`trace/out/matrix_ordinary_player_wither_affected_1/summary.md`. The fail-fast
native gate covers the cadence. The combined Regeneration and Wither aggregate
passes with a 295,708 KB peak and zero swap at
`trace/out/test_runtime_ordinary_regeneration_wither_final.log`; its shared-host
wall time is excluded. These twenty-fourth and twenty-fifth strict rows are
performance-pending while GPU 1 is shared, and the promoted total remains 676.

Two further strict potion candidates implement Strength and Weakness in the
ordinary melee attack-damage attribute. With Strength I, Java's full-cooldown
empty-hand hit rises from one point to four. Old magma keeps potion, cooldown,
immunity, and exhaustion timing exact but leaves the ten-health pig at nine,
first diverging only in target health at
`trace/out/matrix_ordinary_player_strength_probe_1/summary.md`. The exact
eight-tick hit/rejected-follow-up trace passes at
`trace/out/matrix_ordinary_player_strength_candidate_1/summary.md`; the random,
baseline melee, and potion family passes 8/8 at
`trace/out/matrix_ordinary_player_strength_affected_1/summary.md`.

Weakness I reduces the empty-hand attribute below its zero floor. The focused
old-C row reaches the correct zero health delta after the shared attribute
arithmetic, but incorrectly starts hurt immunity and adds attack exhaustion at
tick 4 at `trace/out/matrix_ordinary_player_weakness_probe_1/summary.md`. The
corrected zero-damage rejection still resets swing cooldown without changing
target or exhaustion and passes at
`trace/out/matrix_ordinary_player_weakness_candidate_1/summary.md`. Strength,
Weakness, baseline melee, both random controls, and neighboring potions pass
8/8 at `trace/out/matrix_ordinary_player_weakness_affected_1/summary.md`.
Potion lookup is bounded and occurs only for a queued attack. The combined
aggregate passes with a 296,176 KB peak and zero swap at
`trace/out/test_runtime_ordinary_strength_weakness_final.log`; its shared-host
wall time is excluded. These twenty-sixth and twenty-seventh strict rows are
performance-pending while GPU 1 is shared, and the promoted total remains 676.

Two further strict potion candidates implement Haste and Mining Fatigue in
both ordinary mining speed and the player attack-speed attribute. With Haste
II, Java breaks the staged hand-mined stone within 120 held ticks and reports
an attack cooldown of 0.24 after the first tick. Old magma retains the exact
potion lifecycle but uses its base 0.20 cooldown and leaves the stone intact at
`trace/out/matrix_ordinary_player_haste_mining_probe_1/summary.md`. The exact
120-tick state and stone-to-air result pass at
`trace/out/matrix_ordinary_player_haste_mining_candidate_1/summary.md`.

Mining Fatigue I is paired with the same fixture. The ordinary no-effect
control breaks the stone within 180 ticks, while both Java and corrected magma
retain it through tick 179 and agree on the 0.18 attack cooldown. The strict
row passes at
`trace/out/matrix_ordinary_player_mining_fatigue_candidate_1/summary.md`.
Both random controls, ordinary mining, Speed II expiry, baseline melee,
Strength, Weakness, Haste, and Fatigue pass 9/9 at
`trace/out/matrix_ordinary_player_haste_fatigue_affected_1/summary.md`.
Effect attributes are recomputed only when the bounded potion list changes;
the existing mining kernel consumes cached amplifier integers. The CPU
aggregate passes with a 295,800 KB peak and zero swap at
`trace/out/test_runtime_ordinary_haste_fatigue_final.log`. These twenty-eighth
and twenty-ninth strict rows are performance-pending while GPU 1 is shared,
and the promoted total remains 676.

One further strict potion candidate implements Resistance on the shared
ordinary incoming-damage path. The focused Resistance I cactus fixture is
identical to the unprotected control except for effect 11: Java takes 0.8
points at tick 1, while old magma takes the full point and first diverges only
in health at
`trace/out/matrix_ordinary_player_resistance_probe_1/summary.md`. The exact
four-tick health, hurt, exhaustion, potion, block, and light trace passes at
`trace/out/matrix_ordinary_player_resistance_candidate_1/summary.md`. Nine
affected damage and negative-control cases pass before a repaired fire-contact
checker also passes at
`trace/out/matrix_ordinary_player_resistance_fire_contact_repair_1/summary.md`.
The amplifier is cached when the bounded potion list changes; contact, mob,
magic, and explosion damage reuse constant-time reduction after armor. The
native effect and mob suites pass, including a repaired weighted-plate fixture
that had staged its plate outside the loaded range and over air. The CPU
aggregate passes with a 296,616 KB peak and zero swap at
`trace/out/test_runtime_ordinary_resistance_final.log`; its shared-host timing
is excluded. This thirtieth strict row is performance-pending while GPU 1 is
shared, and the promoted total remains 676.

One further strict potion candidate implements Absorption gold hearts on the
shared ordinary incoming-damage path. A duration-3 Absorption I cactus fixture
consumes one of four absorption points at tick 1, removes the remaining three
when the modifier expires at tick 2, and lets the unprotected tick-11 contact
reach health. Old magma instead loses health and adds attack exhaustion at
tick 1, first diverging in health at
`trace/out/matrix_ordinary_player_absorption_probe_1/summary.md`. The corrected
12-tick health, hurt, exhaustion, potion, block, and light trace passes at
`trace/out/matrix_ordinary_player_absorption_candidate_1/summary.md`.
Resistance, unprotected cactus/fire, periodic magic, regeneration, melee, and
both random controls pass with it in the 11/11 affected family at
`trace/out/matrix_ordinary_player_absorption_affected_1/summary.md`. Gold-heart
state is stored directly in the fixed player/mob runtime, and damage consumes
it in constant time after armor and Resistance. The native effect and mob
suites pass. The CPU aggregate passes with a 294,844 KB peak and zero swap at
`trace/out/test_runtime_ordinary_absorption_final.log`; its shared-host timing
is excluded. This thirty-first strict row is performance-pending while GPU 1
is shared, and the promoted total remains 676.

One further strict potion row closes the Slowness coverage gap. The exact
negative operation-2 modifier was already implemented, so this row records no
invented old-C divergence. A duration-6 Slowness II fixture has five exact
active rows, travels measurably slower than its post-expiry baseline, and
returns to ordinary speed in both engines at
`trace/out/matrix_ordinary_player_slowness_candidate_1/summary.md`. Both random
controls, Speed, Haste, Mining Fatigue, and Slowness pass 6/6 at
`trace/out/matrix_ordinary_player_slowness_affected_1/summary.md`. This test-only
addition changes no runtime hot path. The thirty-second strict row remains in
the current full-source performance-pending batch while GPU 1 is shared, and
the promoted total remains 676.

One further strict potion candidate implements Health Boost's maximum-health
attribute and its interaction with natural regeneration. Direct Java and magma
state now expose both maximum health and absorption instead of inferring them
from damage. The deliberate old-C duration-3 Health Boost II row holds maximum
health at 20 and leaves the food timer at zero, while Java reports maximum
health 28 and advances the timer because health 20 is below the boosted cap at
`trace/out/matrix_ordinary_player_health_boost_probe_1/summary.md`. The
corrected five-tick row matches maximum health `28,28,20,20,20`, food timer
`1,2,0,0,0`, health 20, zero absorption, and exact duration/expiry at
`trace/out/matrix_ordinary_player_health_boost_candidate_2/summary.md`. Both
random controls plus Jump Boost, Fire Resistance, Resistance, Absorption,
Hunger, Poison, Regeneration, and Wither pass 10/10 at
`trace/out/matrix_ordinary_player_health_boost_affected_1/summary.md`. Attribute
recomputation occurs only when the bounded potion list changes; the normal
vitals path reads one cached float. The scalar vitals kernel remains exact
against its 400-line Java golden. This thirty-third strict row is
performance-pending while GPU 1 is shared, and the promoted total remains 676.

Three further strict R-02 candidates make saved lever and button circuitry
playable through the ordinary right-click path. The deliberate old-C boundary
could raycast each wall control but never sent it to the integrated server.
The corrected lever toggles twice, the stone button releases at +20, the
wooden button releases at +30, and both button paths hand their lamp an exact
+4 callback at
`trace/out/matrix_redstone_player_control_use_candidate_2/summary.md`. Every
row matches click cooldown, pending work, all 26 simulated state features,
raw blocks, and block light. Both random controls and seven neighboring
redstone lifecycles pass 9/9 at
`trace/out/matrix_redstone_player_control_use_affected_1/summary.md`. The
native aggregate passes in 10:55 with a 295,220 KB peak and zero swap at
`trace/out/test_runtime_redstone_player_control_use_final.log`. GPU timing is
deferred while GPU 1 is shared.

Two more strict R-03 interaction candidates make repeaters and comparators
playable. A pitched-down physical click cycles a repeater from delay 1 to 2
and a comparator from compare to subtract mode. Old C leaves each block
unchanged and misses only the successful-click cooldown reset at
`trace/out/matrix_redstone_player_diode_use_probe_1/summary.md`. The corrected
metadata transitions `93:2` to `93:6` and `149:2` to `149:6`, with exact state,
raw blocks, light, and no unintended scheduled work at
`trace/out/matrix_redstone_player_diode_use_candidate_2/summary.md`. Affected
random, repeater, comparator, lock, and priority paths pass 9/9 at
`trace/out/matrix_redstone_player_diode_use_affected_1/summary.md`. The final
native aggregate passes with a 295,096 KB peak and zero swap at
`trace/out/test_runtime_redstone_player_diode_use_final.log`. Its 13:03 wall
time is load-contaminated; performance remains deferred on the shared GPU.

Three further strict interaction candidates make wooden doors, fence gates,
and trapdoors authoritative under physical player use. The old upper-door
click cannot resolve its lower half, while the old gate and trapdoor shortcut
mutate the client-local window but never perform the server success swing.
Those failures are isolated at
`trace/out/matrix_ordinary_player_wooden_access_use_probe_1/summary.md`. The
corrected upper oak door toggles its paired lower metadata `1` to `5`, the
opposite-facing oak gate rotates and opens `0` to `6`, and the bottom oak
trapdoor opens `0` to `4`. All three match cooldown, zero pending work, all 26
simulated state features, raw blocks, and light at
`trace/out/matrix_ordinary_player_wooden_access_use_candidate_1/summary.md`.
Iron door and trapdoor use remain exact refusals. Affected and aggregate
controls pass 10/10 at
`trace/out/matrix_ordinary_player_wooden_access_use_affected_1/summary.md`.
The later exact-source aggregate includes this family and passes at
`trace/out/test_runtime_redstone_daylight_detector_use_final.log`;
performance remains deferred on the shared GPU.

Two further strict R-02 candidates add physical normal/inverted daylight
detector use. Old magma raycasts both targets but misses the successful click
and leaves the block unchanged at
`trace/out/matrix_redstone_player_daylight_detector_use_probe_1/summary.md`.
The corrected noon transitions are `151:15` to `178:0` and `178:0` to
`151:15`, derived from exact saved skylight, world time, the vanilla celestial
angle, and clear-weather sky subtraction at
`trace/out/matrix_redstone_player_daylight_detector_use_candidate_2/summary.md`.
Detector metadata now supplies weak power to the represented redstone
consumers. Both random controls plus detector collision, piston mobility,
lever, repeater, and comparator neighbors pass 10/10 at
`trace/out/matrix_redstone_player_daylight_detector_use_affected_1/summary.md`.
The exact-source aggregate passes in 5:37 with a 294,856 KB peak and zero swap
at `trace/out/test_runtime_redstone_daylight_detector_use_final.log`.
Performance remains deferred on the shared GPU.

The periodic half of that R-02 producer is now active as well. A stale normal
detector restored at total time 140 changes `151:0` to `151:15` on Java's next
20-tick boundary; old magma misses that sole raw cell at
`trace/out/matrix_redstone_daylight_detector_periodic_probe_1/summary.md`.
The corrected fixed-capacity detector tile list performs no world scan and
passes the strict metadata boundary at
`trace/out/matrix_redstone_daylight_detector_periodic_candidate_1/summary.md`.
Two circuit cases additionally prove a normal noon detector lights its lamp
and an inverted noon detector releases a lit lamp, with exact block light and
skylight at
`trace/out/matrix_redstone_daylight_detector_periodic_lamp_candidate_2/summary.md`.
That circuit proof also corrected the legacy light-opacity table for detector
IDs 151/178. The 13 neighboring random, interaction, collision, piston, and
lamp outcomes are exact across
`trace/out/matrix_redstone_daylight_detector_periodic_light_affected_1/summary.md`
and the stable rerun at
`trace/out/matrix_redstone_daylight_detector_periodic_light_affected_rerun_1/summary.md`.
Weather-strength attenuation remains F-01 work. Performance remains deferred
while GPU 1 is shared.

Two strict ordinary-interaction candidates add the complete physical cake
lifecycle. With food 18, old magma misses Java's first bite `92:0` to `92:1`,
food `18` to `20`, saturation `5.0` to `5.4`, and success swing at
`trace/out/matrix_ordinary_player_use_cake_probe_2/summary.md`; the corrected
row is exact at
`trace/out/matrix_ordinary_player_use_cake_candidate_1/summary.md`. A second
40-tick case starts at food 6, takes four centered bites, turns 15 degrees to
track the receding cake hitbox, takes the final three servings, and removes
the block exactly at
`trace/out/matrix_ordinary_player_eat_whole_cake_candidate_3/summary.md`.
Random controls plus cake comparator, collision, piston destruction, mob
push, and comparator-power-off paths pass 9/9 at
`trace/out/matrix_ordinary_player_use_cake_affected_1/summary.md`.
The exact-source aggregate through cake passes in 5:50 with a 296,124 KB peak
and zero swap at
`trace/out/test_runtime_daylight_periodic_cake_final.log`.
Performance remains deferred while GPU 1 is shared.

One strict ordinary-interaction candidate adds physical flower-pot insertion.
The valid 60-degree click probe shows Java's client consuming held red flower
`38:2`, then the server committing that item/meta to the empty pot tile one
tick later. Old magma instead places a stray red-flower block and leaves the
pot empty at
`trace/out/matrix_ordinary_player_pot_red_flower_probe_2/summary.md`. The
corrected predicted-client/authoritative-server split, including Java's late
success-swing boundary, passes exact state, inventory, tile contents, raw
blocks, and light at
`trace/out/matrix_ordinary_player_pot_red_flower_candidate_3/summary.md`.
Native coverage exhausts all 21 canonical pottable item/meta pairs plus
occupied and invalid-item refusals. Random, collision, piston-push, and
occupied-pot destruction neighbors pass 7/7 at
`trace/out/matrix_ordinary_player_pot_red_flower_affected_1/summary.md`.
The exact-source aggregate through flower pots passes in 5:41 with a 296,388
KB peak and zero swap at
`trace/out/test_runtime_daylight_periodic_cake_flower_pot_final.log`.
Performance remains deferred while GPU 1 is shared.

One strict ordinary-interaction candidate adds record insertion into an empty
jukebox. Old magma leaves the tile empty, metadata at zero, the record held,
and the cooldown untouched at
`trace/out/matrix_ordinary_player_insert_record_13_probe_1/summary.md`. The
corrected server item-use path installs record 13, changes jukebox metadata
`84:0` to `84:1`, consumes the record on the same authoritative tick, and
matches the success swing at
`trace/out/matrix_ordinary_player_insert_record_13_candidate_2/summary.md`.
The oracle now reads held/full inventory from the same locked server boundary
as player vitals and tile entities, removing nondeterministic client packet
latency from this gate. Native coverage exhausts all 12 vanilla records. Both
random controls, cake and flower-pot interactions, and saved jukebox
comparator strengths 1 and 12 pass 7/7 at
`trace/out/matrix_ordinary_player_insert_record_13_affected_2/summary.md`.
Filled-jukebox use is exact as well. Record 13 and record wait both clear the
tile and metadata, reset the ordinary success cooldown, and create an exact
record EntityItem. The second case forces the spawn box to overlap the
still-present jukebox and proves `Entity.pushOutOfBlocks`, including the
pinned constructor-entropy draw, exact position, velocity, yaw, age, pickup
delay, and entity ID. Both focused rows and five insertion/cake/flower/
comparator controls pass 7/7 at
`trace/out/matrix_ordinary_player_eject_record_affected_1/summary.md`. Native
coverage ejects all 12 records and proves a full item pool rejects ejection
without changing tile state, RNG, or the entity cursor. Record sound event
1010 and audio playback remain A-01 work. Performance remains deferred while
GPU 1 is shared.

Three further strict R-02 candidates add redstone-powered TNT ignition and the
complete 79-tick pre-explosion fuse trajectory. The neighbor-power old-C probe
leaves block 46 in place and creates no entity, while Java removes the block,
allocates one `EntityTNTPrimed`, and advances the exact constructor RNG and
entity-ID cursors at
`trace/out/matrix_redstone_tnt_ignite_probe_1/summary.md`. A separate direct
placement probe proves the `BlockTNT.onBlockAdded` boundary: Java leaves the
powered target air and creates the same primed entity while old C installs TNT
at
`trace/out/matrix_redstone_tnt_direct_add_probe_1/summary.md`. Both corrected
ignition paths, plus fuse decrement, gravity, Y-X-Z collision motion, drag,
and grounded bounce through fuse 1, match the Java entity and controlled
cursors exactly. The neighboring redstone/jukebox family passes 8/8 at
`trace/out/matrix_redstone_tnt_affected_2/summary.md`. The fixed active set is
untouched when empty and performs no world scan. The exact-source aggregate
passes in 5:16.39 with a 295,856 KB peak and zero swap at
`trace/out/test_runtime_redstone_tnt_ignition_fuse_final.log`. Fuse-zero
handling now has strict crater and chain slices below. Primed-TNT rendering,
capsule restore, and the prime sound remain separate open work; the existing
generic explosion kernel is not by itself a vanilla TNT substitute. GPU timing
remains deferred while GPU 1 is shared, so the promoted total remains 676.

Two strict ordinary-use candidates add both legal player ignition items for
TNT. The deliberate flint-and-steel old-product probe leaves TNT 46 in place,
lights the adjacent air cell, and damages the tool one client tick early while
Java removes TNT on the authoritative server boundary and creates one
`EntityTNTPrimed` at
`trace/out/matrix_ordinary_player_ignite_tnt_flint_probe_1/summary.md`. The
corrected flint and fire-charge rows match held-stack durability/consumption,
success cooldown, raw blocks, block light, entity ID, constructor RNG, and the
immediate fuse-79 motion row at
`trace/out/matrix_ordinary_player_ignite_tnt_items_candidate_1/summary.md`.
The fire-charge comparison additionally closed vanilla's empty-main-hand
cooldown reset after consuming the last charge. Both item paths and eight
neighboring redstone/jukebox cases pass 10/10 at
`trace/out/matrix_ordinary_player_ignite_tnt_items_affected_1/summary.md`.
Native coverage adds invalid-item refusal and full primed-entity-pool atomicity.
The exact-source aggregate passes under two-core validation load in 5:39.24
with a 298,736 KB peak and zero swap at
`trace/out/test_runtime_tnt_player_ignition_final.log`. The physical-use path
is action-driven and adds no idle scan. GPU timing remains deferred while GPU
1 is shared, so the promoted total remains 676.

Two strict R-02 cases add burning-arrow TNT collision ignition and its
non-burning control. The deliberate old-C probe retains TNT while Java removes
the block and creates a fuse-79 `EntityTNTPrimed` at
`trace/out/matrix_redstone_tnt_burning_arrow_probe_1/summary.md`. The corrected
runtime ages arrow fire before collision, rejects an arrow whose final fire
tick has expired, primes TNT at contact, and advances the new entity on that
same authoritative boundary. Three independent burning and three non-burning
captures pass 6/6 at
`trace/out/matrix_redstone_tnt_burning_arrow_repeat_3/summary.md`; both rows and
ten neighboring arrow, redstone, piston, and TNT controls pass 12/12 at
`trace/out/matrix_redstone_tnt_burning_arrow_affected_2/summary.md`. The oracle
pins constructor cursors only at the armed burning-arrow/TNT contact, excluding
unrelated same-tick world-generation entity construction from the narrow
causal proof. Native coverage proves fire 100 becomes 99 and ignites, while
fire 1 becomes 0 and does not. The implementation visits only the fixed active
projectile set and adds no idle world scan. The exact-source aggregate passes
under unrelated host training load in 5:40.55 with a 303,104 KB peak and zero
swap at `trace/out/test_runtime_tnt_burning_arrow_final.log`. The bounded
fuse-zero crater and explosion-triggered short-fuse slices are staged below.
GPU timing remains deferred while GPU 1 is shared, so the promoted total
remains 676.

One strict R-02 fuse-zero slice now covers an isolated no-drop TNT crater. A
one-tick saved `EntityTNTPrimed` old-C probe retires the entity but leaves all
six surrounding glass cells intact, while Java removes exactly those cells at
`trace/out/matrix_tnt_fuse_zero_glass_probe_2/summary.md`. The corrected live
ray path consumes all 1,352 vanilla `World.rand.nextFloat()` density draws and
the two `doExplosionB` sound-pitch draws, uses the promoted Minecraft stone
blast resistance at the comparison-volume floor, removes the six glass blocks,
and leaves the distant player and entity set exact. Three independent captures pass 3/3 at
`trace/out/matrix_tnt_fuse_zero_glass_repeat_1/summary.md`; the new row plus 12
neighboring ignition, arrow, piston, lamp, and fuse controls pass 13/13 at
`trace/out/matrix_tnt_fuse_zero_glass_affected_1/summary.md`. The live ray work
runs only when an explosion occurs. This does not yet claim general explosion
parity: drops, fire, exposure-based entity damage and knockback, the complete
resistance table, emitted sound, particles, and rendering remain open.
The exact-source aggregate passes under shared-host load in 5:35.70 with a
299,292 KB peak and zero swap at
`trace/out/test_runtime_tnt_fuse_zero_glass_final.log`. GPU timing remains
deferred while GPU 1 is shared, so the promoted total remains 676.

One further strict R-02 slice covers explosion-triggered TNT chain priming. The
clean old-product probe removes the six hit blocks but produces no replacement
entity at
`trace/out/matrix_tnt_explosion_chain_prime_probe_1/summary.md`. The corrected
path removes five glass cells and the hit TNT, allocates the next exact entity
ID, consumes two constructor `Math.random` draws, samples the short fuse with
the post-sound `World.rand.nextInt(20)`, and advances the new fuse from 10 to 9
on the same tick. The strengthened checker derives every expected cursor and
fuse from the saved pre-detonation state. Three independent captures pass 3/3
at `trace/out/matrix_tnt_explosion_chain_prime_repeat_1/summary.md`; the chain,
glass crater, and 12 neighboring ignition, arrow, piston, lamp, and fuse cases
pass 14/14 at
`trace/out/matrix_tnt_explosion_chain_prime_affected_1/summary.md`. Native
coverage also proved that a leftover TNT fixture inside the no-chain crater
correctly becomes a chain entity, after which that test was explicitly
isolated. The exact-source aggregate passes in 5:16.98 with a 303,588 KB peak
and zero swap at
`trace/out/test_runtime_tnt_explosion_chain_prime_final_2.log`. Multiple hit
TNTs, Java affected-block iteration ordering, drops, fire, remaining
specialized and shaped ray targets, armor/resistance variants, non-player entities,
emitted sound, particles, and rendering remain open. The path is
explosion-event-only and adds no idle scan. GPU timing
remains deferred while GPU 1 is shared, so the promoted total remains 676.

One strict R-02 slice now covers unarmored player damage and packet knockback
from an isolated open-air TNT explosion. The old-product probe applies seven
points from its prior eye-centered approximation but creates no hurt state or
knockback, while Java applies three points, 0.1 exhaustion, and the tracked
velocity response at
`trace/out/matrix_tnt_explosion_player_open_air_probe_2/summary.md`. The
corrected boundary uses feet for damage range, eye height for impulse direction,
and open-air density one; it then models the self-tracking velocity packet's
1/8000 truncation before the client travel tick. Java and magma both finish at
health 17, hurt time 9, X 8.409875, and X velocity
-0.0492082557156682 at
`trace/out/matrix_tnt_explosion_player_open_air_candidate_3/summary.md`. Three
independent captures pass 3/3 at
`trace/out/matrix_tnt_explosion_player_open_air_repeat_1/summary.md`; the new
row and 14 neighboring crater, chain, ignition, arrow, piston, lamp, and fuse
controls pass 15/15 at
`trace/out/matrix_tnt_explosion_player_open_air_affected_1/summary.md`. Native
coverage stages the same clear corridor and checks the causal world cursor,
damage/hunger lifecycle, and client packet response. The full CPU aggregate
passes in 5:13.29 with a 301,368 KB peak and zero swap at
`trace/out/test_runtime_tnt_explosion_player_open_air_final.log`. The first
obstructed-density slice follows below. Armor and resistance variants,
non-player entities, drops, fire, sound, particles, and rendering remain open.
The work runs only on an explosion event and adds no idle scan. GPU timing
remains deferred while GPU 1 is shared, so the promoted total remains 676.

One bounded R-02 diagnostic now covers full-cube obstruction of player blast
exposure. One no-drop glass cube blocks 21 of the standing player's 45 vanilla
sample rays, producing float density 0.53333336, damage 4, and packet X
-0.09516628. The old-product probe hardcodes density one, over-damages the
player to health 13, and reaches X 8.321625 at
`trace/out/matrix_tnt_explosion_player_obstructed_probe_4/summary.md`. The
corrected pre-removal event boundary reproduces health 16, hurt time 9,
quantized X 8.404875, client X velocity -0.05193825603276491, and authoritative
server X velocity -0.051960797397907814. Three independent captures pass 3/3
at
`trace/out/matrix_tnt_explosion_player_obstructed_candidate_2/summary.md`.
The row is bounded diagnostic rather than strict because the real Java client
has independently demonstrated a one-observation race when receiving the
explosion packet; density, damage, packet impulse, server motion, raw blocks,
and C response remain exact whether that client packet is applied or deferred.
The new row and 15 neighboring crater, chain, ignition, arrow, piston, lamp,
and fuse controls pass 16/16 at
`trace/out/matrix_tnt_explosion_player_obstructed_affected_1/summary.md`.
A new focused native test pins the same-seed crater and passes in 23.49 seconds
with a 37,460 KB peak. The full CPU aggregate passes in 6:05.53 with a 307,616
KB peak and zero swap at
`trace/out/test_runtime_tnt_explosion_player_obstructed_final_3.log`. Standing
player density uses 45 short rays only when an explosion occurs; there is no
idle scan or allocation. Non-collidable ray targets, broader shaped occluders,
and non-player entities remain open. GPU
timing remains deferred while GPU 1 is shared, so the promoted total remains
676.

Four bounded R-02 correctness candidates now cover open-air TNT defense:
Resistance I, one plain diamond chestplate, Blast Protection IV on that
chestplate, and the combined vanilla order of armor, Resistance, enchantment
protection, then absorption. The oracle equips armor before the parked
boundary so Java rebuilds armor/toughness attributes, then records the post-hit
armor stack separately from the capsule's main-inventory-only contract. Plain
armor reduces raw damage 3 to 2.184 and finishes at health 17.816; Blast
Protection IV reduces that to 1.4851201 and finishes near health 18.51488; the
combined Resistance case takes 1.188096 and finishes near health 18.811905.
All paths increment chestplate durability from zero to one and preserve
enchantment ID 3 level 4 exactly. Java 1.11.2 floors the sub-one Blast
Protection knockback reduction to zero here, so client and authoritative
server motion retain the exact unmodified TNT values. The plain and enchanted
pair passes at
`trace/out/matrix_tnt_explosion_player_defense_candidate_2/summary.md`; the
combined case passes three independent captures at
`trace/out/matrix_tnt_explosion_player_defense_combined_candidate_1/summary.md`.
Crater, chain, open-air, obstructed, and all four defense rows pass 8/8 at
`trace/out/matrix_tnt_explosion_player_defense_regression_1/summary.md`.
The native test retains one integrated explosion world and checks the three
damage formulas directly, keeping its cost to 24.92 seconds and 37,444 KB.
The full CPU aggregate passes in 5:32.46 with a 308,096 KB peak and zero swap
at `trace/out/test_runtime_tnt_explosion_player_defense_final.log`. Armor work
is event-only and bounded to four slots; it adds no idle world scan or heap
allocation. GPU timing remains deferred while GPU 1 is shared, so these four
correctness rows remain performance-pending and the promoted total remains
676.

Four strict R-02 slices now cover TNT response for a non-player living entity.
The paired saved-state fixture spawns a locked pig before a fuse-one TNT so
the existing entity ordering is explicit and the blast cannot touch the
player or any block. The old-product probe leaves the pig at health 10 with
zero motion at
`trace/out/matrix_tnt_explosion_mob_probe_1/summary.md`. The corrected
event-only path samples the pig's exact 0.9 by 0.9 AABB, uses feet for range
and float-derived eye height for impulse direction, applies nine damage, and
finishes at health 1 with motion X -0.2480964714222078, motion Y
0.030753624425608167, hurt time 10, and hurt-resistant time 20. Java and magma
match every simulated state feature and the zero-block-mutation control at
`trace/out/matrix_tnt_explosion_mob_candidate_2/summary.md`; the crater,
chain, six player exposure/defense variants, and pig row pass 9/9 at
`trace/out/matrix_tnt_explosion_living_regression_1/summary.md`. The focused
native test remains bounded at 23.93 seconds and 37,496 KB. The full CPU
aggregate passes in 5:55.93 with a 308,092 KB peak and zero swap at
`trace/out/test_runtime_tnt_explosion_mob_final.log`.

The second row reverses saved entity order so fuse-one TNT updates before the
pig. The old runtime still groups controlled mobs first and therefore leaves
fresh motion/hurt values at
`trace/out/matrix_tnt_explosion_mob_tnt_first_probe_1/summary.md`. The
corrected bounded scheduler respects the saved entity IDs: after TNT applies
the hit, Java and magma both run the pig's same-tick NoAI update, aging hurt
10 to 9 and invulnerability 20 to 19 while damping motion by 0.98 to X
-0.24313454199376364 and Y 0.030138551937096004. Three independent captures
pass 3/3 at
`trace/out/matrix_tnt_explosion_mob_tnt_first_repeat_1/summary.md`, both order
rows pass strict after the final scheduler hardening at
`trace/out/matrix_tnt_explosion_mob_order_final_1/summary.md`, and the complete
TNT family passes 10/10 at
`trace/out/matrix_tnt_explosion_ordered_living_regression_1/summary.md`. The
focused native order test passes in 24.57 seconds with a 37,440 KB peak. The
exact-source CPU aggregate passes in 6:05.70 with a 308,416 KB peak and zero
swap at `trace/out/test_runtime_tnt_explosion_mob_order_final.log`.

The lethal pair begins the same pig at health 8 with mob loot disabled. When
the pig precedes TNT, both engines expose health 0 and death time 0 on the
blast boundary, age death time 1 through 19, and retire the entity at tick 20.
When TNT precedes the pig, its same-tick update advances death time to 1 and
retirement occurs at tick 19. Three independent pig-first captures pass at
`trace/out/matrix_tnt_explosion_mob_lethal_repeat_1/summary.md`; all four
surviving/lethal order rows pass strict at
`trace/out/matrix_tnt_explosion_mob_living_order_final_1/summary.md`. These
paths add no idle scan or heap allocation; the bounded target walk and
exposure rays run only with active controlled TNT. Armored mobs, loot drops,
projectiles, interleaved multi-entity order, and multi-entity blasts remain
separate work. GPU timing remains deferred while GPU 1 is
shared, so these four correctness rows are performance-pending and the
promoted total remains 676.

A fifth strict R-02 living-explosion slice covers an outline-only occluder.
Java `World.getBlockDensity` calls `rayTraceBlocks` with
`ignoreBlockWithoutBoundingBox=false`, while the old runtime reused the
projectile path's `true` flag and skipped a standing torch because its entity
collision box is null. The supported-torch negative retains exact blocks and
light but over-damages the pig from health 10 to 1 at
`trace/out/matrix_tnt_explosion_mob_torch_occluded_probe_2/summary.md`.
Java measures exposure 2/3: damage is 6, health is 4, and fresh motion is X
-0.1653976525440392 and Y 0.0205024168947584. The corrected common ray kernel
keeps projectile filtering separate from explosion `canCollideCheck` and uses
the existing exact outline boxes. The strict candidate passes at
`trace/out/matrix_tnt_explosion_mob_torch_occluded_candidate_1/summary.md`,
and three independent captures pass 3/3 at
`trace/out/matrix_tnt_explosion_mob_torch_occluded_repeat_1/summary.md`.
Across the 13-case crater/chain/player/living family, all behavior and raw
block gates pass; eight state rows are strict and five declared player packet
rows remain bounded diagnostics at
`trace/out/matrix_tnt_explosion_outline_regression_1/summary.md`. The focused
native test combines the outline ray with TNT-first damping and passes in
22.68 seconds with a 37,464 KB peak. The full CPU aggregate passes in 5:35.75
with a 308,148 KB peak and zero swap at
`trace/out/test_runtime_tnt_explosion_outline_final.log`. The extra outline
work runs only inside active explosion exposure rays; it adds no idle scan or
allocation. Specialized collision-ray overrides and additional shaped
occluders remain explicit follow-up work. GPU timing remains deferred while
GPU 1 is shared, so this correctness row is performance-pending and the
promoted total remains 676.

A first strict R-02 dropped-entity explosion slice covers a surviving stone
`EntityItem` seven blocks from fuse-one TNT. The deliberate old-C probe leaves
item health at 5 and motion zero while Java applies damage 4 and the raw
explosion impulse at
`trace/out/matrix_tnt_explosion_item_surviving_probe_1/summary.md`. The exact
fixture also exposes saved loaded-entity order: the item ID precedes TNT, so
its stationary update advances age to 1 before detonation and the fresh
impulse must not be moved or damped again. The corrected event path samples
the item's 0.25-cube AABB, uses feet for range and float-derived eye height for
direction, changes health 5 to 1, retains position 9.5/83/8.5, and produces X
-0.12494932090351177 and Y 0.003413794015269717. The strict candidate passes
at
`trace/out/matrix_tnt_explosion_item_surviving_candidate_1/summary.md`, three
independent captures repeat 3/3 at
`trace/out/matrix_tnt_explosion_item_surviving_repeat_1/summary.md`, and every
behavior/raw-block gate in the expanded 14-case TNT family passes at
`trace/out/matrix_tnt_explosion_item_regression_1/summary.md`. The integrated
native regression passes in 22.96 seconds with a 37,440 KB peak. The full CPU
aggregate passes in 5:50.37 with a 306,756 KB peak and zero swap at
`trace/out/test_runtime_tnt_explosion_item_final.log`. EntityItem enumeration
uses the fixed 48-slot store only during an active explosion, and the saved
order check is entered only with active primed TNT, so no idle world scan or
allocation was added. Lethal/protected item cases and
general interleaved entity order remain follow-up work. GPU timing is deferred
while GPU 1 is shared, so this correctness row is performance-pending and the
promoted total remains 676.

A second strict dropped-item slice reverses saved order so fuse-one TNT has the
lower entity ID. The old path applies damage and the raw impulse but leaves the
item at its original position with undamped motion at
`trace/out/matrix_tnt_explosion_item_tnt_first_probe_1/summary.md`. Java's
same-tick gravity-free `EntityItem.onUpdate` moves to X 9.375050679096487 and Y
83.00341379401527, then multiplies motion by float 0.98 to X
-0.12245033686866069 and Y 0.003345518200077276. The corrected candidate
passes at
`trace/out/matrix_tnt_explosion_item_tnt_first_candidate_1/summary.md`, and
three independent captures repeat 3/3 at
`trace/out/matrix_tnt_explosion_item_tnt_first_repeat_1/summary.md`. Restoring
normal movement only for externally accelerated gravity-free fixtures keeps
zero-motion pressure-plate items parked; both TNT orders, wooden and weighted
plate controls, and representative downward/horizontal piston pushes pass at
`trace/out/matrix_tnt_explosion_item_order_affected_3/summary.md`. The
final-source native test passes in 23.65
seconds with a 37,456 KB peak at
`trace/out/test_tnt_explosion_item_order_final.log`; the full CPU aggregate
passes in 5:27.94 with a 306,960 KB peak and zero swap at
`trace/out/test_runtime_tnt_explosion_item_order_final.log`. General
interleaved multi-entity order remains follow-up work.
GPU timing is deferred while GPU 1 is shared, so the promoted total remains
676.

Two more strict item-explosion controls close the ordinary lethal and protected
stack branches. At six blocks, Java damage exceeds the stone item's private
health 5 and both engines retire it on the blast tick. At seven blocks, a
Nether Star rejects explosion damage, retains health 5, and still receives the
exact X -0.12494932090351177/Y 0.003413794015269717 impulse because
`Explosion.doExplosionA` adds motion even when `attackEntityFrom` returns
false. Both explicit behavior gates pass at
`trace/out/matrix_tnt_explosion_item_lifecycle_candidate_1/summary.md`, and
both repeat 3/3 at
`trace/out/matrix_tnt_explosion_item_lifecycle_repeat_1/summary.md`. Focused
native checks cover the quarter-cube target AABB, lethal retirement, and the
Nether Star exception in 23.92 seconds with a 37,468 KB peak at
`trace/out/test_tnt_explosion_item_lifecycle_final.log`. The final runtime
source is unchanged from the 5:27.94 aggregate above. Occluded/multiple items,
tagged-item cleanup, and general interleaved entity order remain follow-up
work. GPU timing is deferred while GPU 1 is shared, so these correctness rows
remain performance-pending and the promoted total remains 676.

A strict TNT-first boat slice now closes the next represented non-living
entity category. The saved fixture gives fuse-one TNT the lower entity ID and
places a gravity-free boat seven blocks away. The old path retires TNT but
leaves the boat parked with zero motion at
`trace/out/matrix_tnt_explosion_boat_probe_1/summary.md`. The corrected
event-only path includes the boat's exact 1.375 by 0.5625 AABB, applies damage
4 as damage-taken 40 without destroying it, then runs the same-tick boat
update. Float 0.9 momentum produces position X 9.387742502803361, Y
83.00814089616274 and motion X -0.11225749719663904, Y
0.008140896162744845. The explicit behavior/state/block gate repeats 3/3 at
`trace/out/matrix_tnt_explosion_boat_repeat_1/summary.md`; both pig orders,
both item orders, boat tripwire occupancy, and the stone-plate boat negative
also pass 7/7 at
`trace/out/matrix_tnt_explosion_boat_affected_1/summary.md`. The focused native
test passes in 23.58 seconds with a 37,512 KB peak at
`trace/out/test_tnt_explosion_boat_final.log`, and the full CPU aggregate
passes in 5:42.36 with a 306,756 KB peak and zero swap at
`trace/out/test_runtime_tnt_explosion_boat_final.log`. Target enumeration is a
fixed-capacity active-explosion branch and controlled boat work remains behind
an existing active-entity branch, with no idle scan or allocation. GPU timing
is deferred while GPU 1 is shared, so the promoted total remains 676.

A strict arrow-first TNT slice closes the first represented projectile
category. The saved arrow updates once before fuse-one TNT, so Java retains
its position and zero rotation, then adds raw explosion motion X
-0.12499536788906258 and Y -0.0003794502612004625. The old product leaves
motion zero at
`trace/out/matrix_tnt_explosion_arrow_probe_1/summary.md`. The corrected
event-only path samples the exact 0.5-cube arrow AABB and uses
`EntityArrow.getEyeHeight()==0`, while keeping the arrow's pre-blast rotation
for this boundary. The explicit behavior/state/block gate repeats 3/3 at
`trace/out/matrix_tnt_explosion_arrow_repeat_1/summary.md`. Burning and
non-burning TNT contact, arrow button/tripwire/stone-plate controls, and
representative mob/item/boat explosion rows all pass 9/9 at
`trace/out/matrix_tnt_explosion_arrow_affected_1/summary.md`. The focused
native test passes in 23.61 seconds with a 37,476 KB peak at
`trace/out/test_tnt_explosion_arrow_final.log`; the full CPU aggregate passes
in 5:43.21 with a 306,640 KB peak and zero swap at
`trace/out/test_runtime_tnt_explosion_arrow_final.log`. The only added scan is
the fixed 32-slot projectile array during an active explosion, with no idle
work or allocation. GPU timing is deferred while GPU 1 is shared, so the
promoted total remains 676.

A strict XP-orb-first TNT slice closes the next represented non-living entity
category. The saved orb updates before fuse-one TNT, so its attraction,
gravity, move, drag, age, and color counters advance before the explosion.
The old product matches that pre-blast update but leaves private health
unobserved and motion unchanged at
`trace/out/matrix_tnt_explosion_xp_probe_1/summary.md`. The corrected
event-only path exposes the orb's private health, samples its exact 0.5-cube
AABB and float-derived 0.425 eye height, applies damage 4, and adds the raw
blast impulse. Java and magma both finish at health 1, age 1, position X
9.495054894848845/Y 82.949280010099, motion X -0.12902424922385383/Y
-0.0434473581470098, color 1, and target color 0. The explicit
behavior/state/block gate repeats 3/3 at
`trace/out/matrix_tnt_explosion_xp_repeat_1/summary.md`. XP pickup, XP
tripwire/stone-plate controls, and representative mob/item/boat/arrow blast
rows pass 9/9 at
`trace/out/matrix_tnt_explosion_xp_affected_1/summary.md`. The focused native
test passes in 23.59 seconds with a 37,460 KB peak at
`trace/out/test_tnt_explosion_xp_final.log`; the full CPU aggregate passes in
6:01.94 with a 306,612 KB peak, zero swap, and exit 0 at
`trace/out/test_runtime_tnt_explosion_xp_final.log`. The fixed 16-orb scan
runs only during an active explosion and adds no idle work or allocation. GPU
timing is deferred while GPU 1 is shared, so the promoted total remains 676.

A strict TNT-first falling-sand slice closes the next represented entity
category without adding a new fixture protocol. A due scheduled sand callback
creates the entity one boundary after fuse-three TNT begins ticking; on the
following boundary TNT has the lower saved ID and explodes before the sand's
second update. The old product advances the sand first and applies no blast,
leaving it at X 9.5 with zero X motion at
`trace/out/matrix_tnt_explosion_falling_sand_probe_3/summary.md`. The
corrected scheduler defers only the bounded later-ID falling entity, and the
active explosion samples its exact 0.98-cube AABB and float-derived eye
height. The subsequent sand update applies gravity, movement, and float 0.98
drag, producing exact X 9.376391166346687, Y 82.90807990783813, motion X
-0.12113665933789819, and motion Y -0.06068168302984159. The explicit
three-boundary behavior/state/block gate repeats 3/3 at
`trace/out/matrix_tnt_explosion_falling_sand_repeat_1/summary.md`. Ordinary
sand landing, sand tripwire, crater/chain, and representative mob/item/boat/
arrow/XP explosion rows pass 10/10 at
`trace/out/matrix_tnt_explosion_falling_sand_affected_1/summary.md`. The
focused native test passes in 25.35 seconds with a 37,480 KB peak at
`trace/out/test_tnt_explosion_falling_sand_final.log`; the full CPU aggregate
passes in 5:44.24 with a 307,048 KB peak, zero swap, and exit 0 at
`trace/out/test_runtime_tnt_explosion_falling_sand_final.log`. The fixed
16-entry falling store is inspected only when both falling sand and an active
explosion exist, with no idle world scan or allocation. GPU timing remains
deferred while GPU 1 is shared, so the promoted total remains 676.

A strict TNT-first small-fireball slice closes the next represented projectile
category. Fuse-one TNT has the lower saved entity ID, so Java explodes before
the stationary fireball update. The old product retires TNT but leaves the
fireball parked with zero motion at
`trace/out/matrix_tnt_explosion_small_fireball_probe_1/summary.md`. The
corrected active-explosion path samples the exact 0.3125-cube AABB and
float-derived eye height, then defers the later-ID fireball update until after
detonation. That update moves by the fresh impulse, rotates through vanilla's
table-based `MathHelper.atan2`, and applies float-0.95 damping. Java and magma
finish at exact X 9.37508015347804, Y 83.00436104103332, motion X
-0.11867385270670164, motion Y 0.004142988929661038, yaw 342, and float pitch
-0.39989015. The explicit behavior/state/block gate passes at
`trace/out/matrix_tnt_explosion_small_fireball_behavior_1/summary.md` and
repeats 3/3 at
`trace/out/matrix_tnt_explosion_small_fireball_repeat_1/summary.md`. Ten
neighboring TNT/entity rows and both stationary small-fireball tripwire
controls pass 11/11 at
`trace/out/matrix_tnt_explosion_small_fireball_affected_1/summary.md`; the
ordinary active eight-tick trajectory remains exact at
`trace/out/small_fireball_trajectory_tnt_final`. The focused native test
passes in 23.97 seconds with a 44,640 KB peak at
`trace/out/test_tnt_explosion_small_fireball_final.log`; the full CPU aggregate
passes in 5:38.06 with a 306,696 KB peak, zero swap, and exit 0 at
`trace/out/test_runtime_tnt_explosion_small_fireball_final.log`. The fixed
32-slot scan runs only during an active explosion and the new rotation work
runs only for an active small fireball, with no idle allocation or world scan.
GPU timing remains deferred while GPU 1 is shared, so the promoted total
remains 676.

Two strict two-TNT slices close blast response and loaded-order handling for
already-primed TNT. The two-fixture capsule extension preserves distinct
authoritative IDs and fuses. With fuse-one source TNT first, the old product
retires the source but advances the fuse-80 target only by gravity at
`trace/out/matrix_tnt_explosion_primed_tnt_probe_1/summary.md`; Java also adds
the raw blast impulse before the target's same-tick gravity/move/float-0.98
update. The corrected target finishes at X 9.375004632110937, Y
82.95962055063286, motion X -0.12249546291537877, motion Y
-0.03957186114996505, and fuse 79. With the target's lower ID, it updates
first and therefore retains X 9.5/Y 82.96000000089407 while the later blast
adds raw motion X -0.1249617182537039/Y -0.04029341494275192. Both explicit
behavior/state/block gates pass together at
`trace/out/matrix_tnt_explosion_primed_tnt_orders_final_2/summary.md`, and each
order repeats 3/3 at
`trace/out/matrix_tnt_explosion_primed_tnt_repeat_1/summary.md` and
`trace/out/matrix_tnt_explosion_primed_tnt_target_first_repeat_1/summary.md`.
Ten prior TNT/entity controls plus both orders pass 12/12 on the final source
at `trace/out/matrix_tnt_explosion_primed_tnt_affected_2/summary.md`. The
focused native test passes in 25.83 seconds with a 46,080 KB peak at
`trace/out/test_tnt_explosion_primed_tnt_orders_final.log`; the full CPU
aggregate passes in 5:47.15 with a 307,080 KB peak, zero swap, and exit 0 at
`trace/out/test_runtime_tnt_explosion_primed_tnt_final.log`. Primed-TNT target
enumeration is active-explosion-only, fixed-capacity, and allocation-free; it
also handles a lone primed TNT hit by a non-TNT explosion. GPU timing remains
deferred while GPU 1 is shared, so the promoted total remains 676. Crystals
and general mixed-type interleaving remain separate follow-up work.

A strict standalone/paired End-crystal slice now closes the ordinary render-
state tick and TNT-triggered response for one saved crystal in the Overworld.
The fixture preserves the authoritative ID, position, `innerRotation`, and
base-plate flag. Its idle control advances rotation 0 to 1 without movement.
In the paired case fuse-one TNT has the lower ID and detonates seven blocks
from the crystal; Java destroys both entities and synchronously creates the
crystal's smoking size-six explosion. With that response disabled, magma
leaves the crystal alive at rotation 1 at
`trace/out/matrix_tnt_explosion_end_crystal_probe_1/summary.md`. The corrected
pair retires both entities, leaves the distant player and 33,825-cell block/
light volume unchanged, and advances `World.rand` by exactly 2,708 states for
the two ray casts and sound-pitch pairs. It repeats 3/3 at
`trace/out/matrix_tnt_explosion_end_crystal_repeat_1/summary.md`; the ordinary
crystal control and 13 neighboring TNT cases pass 14/14 at
`trace/out/matrix_tnt_explosion_end_crystal_affected_final_1/summary.md`.
Focused native coverage passes in 24.95 seconds with a 37,468 KB peak at
`trace/out/test_tnt_explosion_end_crystal_final.log`, and the full CPU
aggregate passes in 5:43.06 with a 306,684 KB peak and zero swap at
`trace/out/test_tnt_explosion_end_crystal_candidate.log`. The inactive CPU
guard passes at 4,312 scalar steps/s at
`trace/out/perf_guard_tnt_end_crystal_cpu_1.json`. The pool and explosion scan
are fixed-capacity and the latter runs only during an active explosion; the
idle empty-pool path is one branch. Blaze and CUDA timing remain deferred
while GPU 1 is shared, so the promoted total remains 676.

The adjacent End-dimension crystal-fire path is also strict. A saved crystal
above an obsidian support advances rotation 0 to 1 and replaces the air at its
own block position with fire on its first tick. With only that runtime write
disabled, Java makes the single raw air-to-fire mutation while magma makes
none at `trace/out/matrix_end_crystal_fire_probe_2/summary.md`. The corrected
state/behavior/raw-block gate repeats 3/3 at
`trace/out/matrix_end_crystal_fire_repeat_1/summary.md`; the Overworld idle
control, End fire row, and TNT-triggered response pass 3/3 together at
`trace/out/matrix_end_crystal_fire_affected_final_1/summary.md`. End ambient
block light outside the captured capsule is excluded from this row; the raw
block transition is exact and the prior diagnostic found no light mismatch
within 15 blocks of the controlled fire. Focused native coverage passes in
26.12 seconds with a 46,092 KB peak and zero swap at
`trace/out/test_end_crystal_fire_final.log`; the full CPU aggregate passes in
5:42.21 with a 306,804 KB peak, zero swap, and exit 0 at
`trace/out/test_runtime_end_crystal_fire_final.log`. The active-crystal-only
branch is allocation-free. Dragon-fight notification and beams are closed by
the later slices below; general mixed-type ordering remains follow-up work,
and GPU timing remains deferred.

Saved End-crystal beam-target state is now exact and reaches the live product
render-view stream. The fixture carries an explicit presence bit because block
coordinate `-1` is valid, plus the three absolute target coordinates. Old
magma loses only `has_beam` at tick 0 while state outside the entity and all
22,869 raw block/light cells remain exact at
`trace/out/matrix_end_crystal_beam_probe_1/summary.md`. The corrected one-tick
row preserves target `(20,102,1)` while rotation advances 0 to 1 and repeats
3/3 at `trace/out/matrix_end_crystal_beam_repeat_1/summary.md`. Idle, beam,
End-fire, and TNT-destruction rows pass 4/4 at
`trace/out/matrix_end_crystal_beam_affected_1/summary.md`. Focused native
coverage passes in 24.97 seconds with a 44,664 KB peak and zero swap, and also
verifies the represented crystal is emitted with its exact ID, rotation,
bottom flag, and beam target at
`trace/out/test_end_crystal_beam_candidate.log`. The full CPU aggregate passes
in 6:04.60 with a 306,856 KB peak, zero swap, and exit 0 at
`trace/out/test_runtime_end_crystal_beam_final.log`; its shared-host wall time
is not used as a performance metric. The CPU guard passes at 4,195 scalar
steps/s at `trace/out/perf_guard_end_crystal_beam_cpu_1.json`. The fixed pool
adds no allocation; the render collector exits after one count check when
empty. That state-only slice precedes the renderer work below; dragon-fight
notification remains open and GPU timing remains deferred.

End-crystal target beams now render through the live CPU product path. The C
emitter transcribes `RenderDragon.renderCrystalBeams`: the bob-adjusted target
translation, two rotations, tapered eight-sided smooth-shaded strip, scrolling
UVs, disabled standard item lighting with the owning entity's lightmap
retained, disabled-cull equivalence, and the real standalone 16x256
`endercrystal_beam.png` with GL-repeat sampling. The tape schema now
retains an explicit presence bit so target `(-1,-1,-1)` remains representable.
The pre-fix real-Java tape has measured missing content; on its isolated sky
strip at tick 14, the canonical pixel tool reports 1,799 differing pixels
before the renderer and 284 after it, an 84.2% reduction. The whole frame moves
from 9.70 to 8.91 mean/channel, but is not promoted as an exact full-scene pixel
gate because known terrain/fog, HUD, crystal-base, and slime classifications
remain in the same capture. Evidence and the aligned triptych are at
`raster/verify/trace/report/tape_scenario_end_crystal_beam_20260803T160737Z.md`
and `trace/out/end_crystal_beam_sky_after.png`.
The exact jar-texture gate and focused numerical geometry/repeat/capacity tests
pass; idle, beam, End-fire, and TNT-destruction state controls pass 4/4 at
`trace/out/matrix_end_crystal_beam_geometry_affected_1/summary.md`. The full
CPU aggregate passes in 4:51.43 with a 306,704 KB peak, zero swap, and exit 0
at `trace/out/test_runtime_end_crystal_beam_geometry_final.log`. The clean
CPU guard passes at 5,044 scalar steps/s at
`trace/out/perf_guard_end_crystal_beam_geometry_cpu_1.json`. The empty path is
one bounded entity-list scan and emits no geometry or allocation. GPU 1 was
shared and was not touched, so the promoted total remains 676 and CUDA timing
is still pending.

Live dragon-fight crystal destruction now follows the represented 1.11.2
notification order. Melee, player arrows, small fireballs, and arena-explosion
chains first mark the crystal dead, complete its synchronous size-six nested
explosion, and only then notify the dragon. Destroying the dragon's current
healing crystal applies the separate 10-point head/explosion damage, while a
survival-player source moves the represented holding-pattern phase to strafe.
The bounded real-Java command probe and shared C kernel agree on all three
discriminators: healing/player is `100 -> 90` plus strafe, non-healing/player
stays `100` plus strafe, and healing/non-player with no player inside the
64-block search stays holding at `90`. Evidence is produced by
`trace/test_dragon_crystal_notification.py`; the affected idle, saved-beam,
End-fire, and TNT-first rows pass 4/4 at
`trace/out/matrix_dragon_crystal_notify_affected_1/summary.md`. Focused
lifecycle and TNT-chain tests pass at
`trace/out/test_dragon_live_crystal_notify.log` and
`trace/out/test_tnt_explosion_dragon_notify.log`; the full CPU aggregate
passes in 4:48.18 with a 306,408 KB peak and zero swap at
`trace/out/test_runtime_dragon_crystal_notify_final.log`. The CPU guard passes
at 4,808 scalar steps/s at
`trace/out/perf_guard_dragon_crystal_notify_final_cpu_1.json`. Respawn-sequence
abort/reset, the complete vanilla phase graph, and lethal crystal-notification
transition remain outside this bounded slice. GPU 1 was not touched, so the
promoted total remains 676.

The live dragon healing-crystal relation, beam, and bounded healer update now
follow the real game. The dragon view carries its own `ticksExisted`, current
healing-crystal presence, position, and `innerRotation`; live crystals use the
same complete type-31 renderer as saved crystals. A focused real-Java
llvmpipe scenario contributes 43 stable frames at
`raster/verify/tapes/scenario_dragon_healing_beam_20260803T173302Z.jsonl` and
passes the structural pixel gate. A controlled beam-off replay proves the new
beam improves tick 10 from 2.72 to 2.71 mean/channel over the whole frame and
10.64 to 10.20 in the beam-containing region after retaining the dragon's
lightmap. The remaining mid-beam residual is measured as `texel-selection`,
so it remains in V-01 rather than being called pixel-exact.

The healer transition itself matches a locked real-Java server probe for all
nine observations at `trace/out/test_dragon_healer_java_c.log`: a living
reference persists between random selection gates, healing occurs before
reselection on dragon tick multiples of ten, a dead reference clears first,
selection consumes the exact one-in-ten entity-RNG gate, nearest wins with
first-on-tie ordering, and Java's dragon-AABB expansion admits the dx=40
boundary fixture. Focused dragon and renderer tests pass, the deterministic
CPU dragon runner passes, and the full CPU aggregate passes in 5:35.65 with a
306,600 KB peak at `trace/out/test_runtime_dragon_healer_1.log`. The frozen CPU
performance guard passes at a 4,984 steps/s median at
`trace/out/perf_guard_dragon_healer_cpu_1.json`. The method is exact for a
given entity-RNG stream; the unimplemented phase graph's other consumers can
still shift that stream in a natural fight, so full phase/RNG interleaving
remains open. GPU 1 was shared and was not touched, leaving CUDA and Blaze
promotion evidence deferred and the promoted total at 676.

A bounded W-01/R-02 bed-explosion slice now distinguishes flaming from smoking.
The real 1.11.2 source proves TNT and End crystals use the non-flaming
`createExplosion` overload, while beds outside the Overworld use
`newExplosion(..., flaming=true, smoking=true)`. The live ray kernel now keeps
affected air positions separately from destroyed non-air blocks. After block
removal, a flaming explosion tests air, a full-block support, and its private
`Explosion.explosionRNG.nextInt(3)` in Java order, then places and schedules
fire through World.rand. A clock-seeded explosion RNG is not present in a Java
world save, so the exact oracle supplies its captured internal cursor; normal
magma play uses a deterministic event-local fallback.

The parked real-Java `Explosion` class and C agree on the sole eligible
obsidian-supported cell for both seeded branches at
`trace/out/test_explosion_fire_java_c.log`: flaming produces exactly one fire
block and advances both random cursors, while the same affected cell remains
air and leaves the explosion cursor untouched when non-flaming. The 128-line
existing Java/CPU explosion kernel remains exact. Focused end-to-end native
coverage passes in 26.47 seconds with an 82,960 KB peak at
`trace/out/test_bed_explosion_fire_1.log`; the full CPU aggregate passes in
5:54.29 with a 306,676 KB peak at
`trace/out/test_runtime_bed_explosion_fire_1.log`. The frozen scalar guard
passes at a 4,414 steps/s median at
`trace/out/perf_guard_bed_explosion_fire_cpu_1.json`. The inactive path does
no work; a flaming event adds one 4 KiB stack bitset and bounded scans. General
multi-support HashSet iteration, drops, the complete resistance table, and
capturing the JVM constructor seed in a neutral state capsule remain open.
GPU 1 was shared and untouched, so CUDA and Blaze evidence stays deferred and
the promoted total remains 676.

The next bounded W-01 slice generalizes the proven BlockFalling lifecycle from
sand to metadata-0 gravel. The scheduled callback now preserves the source
block identity through entity creation, the exact nine-tick gravity/drag
trajectory, terrain-atlas rendering, landing placement, and the landed
block's `+2` stability callback. Before the fix, real Java made the two
expected gravel mutations while magma made none at
`trace/out/matrix_falling_gravel_prefix/summary.md`. The final paired sand and
gravel run passes both cases with 26 simulated features matching, exact
behavior gates, and all 10,625 raw cells exact at
`trace/out/matrix_falling_blocks_java_c_final/summary.md`.

Native coverage includes distinct entity IDs and render item IDs for both
materials, plus rejection of nonzero metadata. The full CPU aggregate passes
in 6:15.92 with a 303,808 KB peak at
`trace/out/test_runtime_falling_gravel_focused_1.log`; the frozen scalar guard
passes at a 5,085 steps/s median at
`trace/out/perf_guard_falling_gravel_cpu_1.json`. The inactive path remains a
fixed-capacity count check plus one scheduled-block comparison, with no new
allocation. Dynamic fluid columns, unsupported landing/drop paths, gravel's
flint-drop RNG, lateral motion, and anvils remain open. GPU 1 was
shared and untouched, so CUDA and Blaze evidence stays deferred and the
promoted total remains 676.

The same W-01 lifecycle now follows Java's complete bounded
`BlockFalling.canFallThrough` material predicate: air, water, lava, and fire.
Three new one-cell landing fixtures cover sand through still water, gravel
through static lava, and sand through fire. Before the fix, each exact Java
trace spawned the falling entity but magma rejected the restored scheduled
callback at capsule admission (`invalid schedule_tick`) at
`trace/out/matrix_falling_passthrough_prefix/summary.md`. The corrected runtime
passes those three cases plus the original air/sand and air/gravel controls
5/5 at `trace/out/matrix_falling_passthrough_final/summary.md`. Every row has
26 matching simulated features, the exact nine-tick trajectory, exact source
removal and material replacement, the exact landed `+2` callback, all 10,625
raw blocks, and all 10,625 block-light cells.

Native coverage asserts the three material identities, both falling block
identities, EID consumption, trajectory, replacement, queue drain, and no
World.rand use. The full CPU aggregate passes in 6:14.21 with a 309,664 KB
peak at `trace/out/test_runtime_falling_passthrough_1.log`; the scalar guard
passes at a 4,346 steps/s median at
`trace/out/perf_guard_falling_passthrough_cpu_1.json`. The predicate adds only
bounded integer comparisons on an active callback and no allocation. Dynamic
fluid columns and flow, fire callbacks, nonreplaceable landing/drop behavior,
gravel flint RNG, lateral motion, and anvils remain open. GPU 1 was shared and
untouched; CUDA and Blaze evidence remains deferred and the promoted total is
still 676.

The next W-01 slice covers a deterministic nonreplaceable landing. A metadata-0
sand or gravel entity that intersects a bottom stone slab stops at the exact
half-block top, cannot place into the occupied slab cell, retires, and drops
one matching item instead of scheduling the landed-block `+2` callback. The
pre-fix loaded-world trace records Java's 11 falling observations and tick-12
item while magma rejects the callback at admission at
`trace/out/matrix_falling_sand_bottom_slab_drop_prefix/summary.md`.

The corrected runtime carries the fractional landing surface and failed-place
branch in its fixed falling store. Item construction consumes exactly four
`Math.random` doubles in Java order, one further entity ID, and no World.rand;
the item then receives its ordinary first update above the slab. A parked
real-Java command calls the actual `EntityFallingBlock.onUpdate` method. Sand
and gravel each match C across all 12 falling updates, the constructor cursor,
and the first item update; three repeats pass at
`trace/out/falling_drop_java_c_repeat_3.log`. The ordinary loaded-world row is
diagnostic only for its ambient entity-set cursor, but both engines pass the
same 11-tick trajectory, tick-12 item type/count/age, sole source removal,
unchanged slab, and exact 10,625-cell block and block-light volumes at
`trace/out/matrix_falling_sand_bottom_slab_drop_candidate/summary.md`.
The five earlier air/water/lava/fire controls remain strict 5/5 at
`trace/out/matrix_falling_blocks_after_slab_drop/summary.md`.

Native coverage pins the fractional stop, source/slab states, absent callback,
item ID/stack/health, motion/yaw, four-call Math cursor, and unchanged
World.rand. The final native aggregate passes in 6:10.03 with a 310,780 KB
peak at `trace/out/test_runtime_falling_slab_drop_final.log`. The scalar guard
passes at a 4,230 steps/s median against the
frozen 4,062 baseline and 3,858.9 floor at
`trace/out/perf_guard_falling_slab_drop_cpu_1.json`. The common live-item path
uses one block lookup and reads metadata only for a slab ID. Other partial
collision shapes, lateral falling motion, anvils, timeouts, and fixed-pool
pressure remain open. GPU 1 was shared and untouched, so CUDA and Blaze
evidence remains deferred and the promoted total stays 676.

The complementary top-half stone-slab case is exact. Its full-block collision
surface lets sand complete the same nine-tick falling trajectory, land in the
air cell at y=78, and schedule the normal `+2` stability callback instead of
taking the bottom-slab item-drop branch. The row passes with 26 matching state
features and exact 10,625-cell block and block-light volumes at
`trace/out/matrix_falling_top_slab_candidate_1/summary.md`; three independent
repeats pass at
`trace/out/matrix_falling_top_slab_repeat_3/summary.md`. The affected family
passes all six strict landing/passthrough/top-slab controls plus the known
diagnostic bottom-slab row at
`trace/out/matrix_falling_top_slab_family/summary.md`. Native coverage pins
entity identity, all nine trajectory points, y=78 placement, unchanged slab,
the `+2` callback lifetime, and unchanged World.rand. The full native aggregate
passes in 6:03.99 with a 326,712 KB peak and zero major faults at
`trace/out/test_runtime_falling_top_slab.log`. The change is one active-entity
slab-meta branch with no allocation or idle scan.

Grass path now supplies the next distinct partial-collision failed-placement
case. Sand follows the exact trajectory through observation 9, clips to the
15/16 surface at y=77.9375 on observation 10, cannot replace the path cell,
and becomes one item without a stability callback. The loaded-world row
matches source removal, unchanged support, exact Y/Y-velocity and item
lifecycle through observation 12 at
`trace/out/matrix_falling_shaped_item_lifecycle_final/summary.md`; all eight
affected falling rows pass at
`trace/out/matrix_falling_grass_path_family/summary.md`. The parked Java
oracle pins Math.random and EID state and agrees with the C runtime on 44
updates and four exact sand/gravel drops across bottom slab and grass path at
`trace/out/test_falling_drop_grass_path.log`. The combined native aggregate
passes in 6:09.90 with 330,292 KB peak RSS, zero major faults, zero swap, and
exit zero at `trace/out/test_runtime_tallgrass_grass_path.log`. The scalar
guard passes at 5,102 steps/s against the frozen 4,062 baseline and 3,858.9
floor at `trace/out/perf_guard_tallgrass_grass_path_cpu_1.json`. This proves
the default `doEntityDrops=true`, metadata-zero support branch; the false
gamerule and broader shapes remain queued. GPU 1 stayed shared and untouched,
so CUDA/Blaze promotion remains deferred and the promoted total stays 676.

Soul sand completes the three-step shaped-support ladder at 1/2, 7/8, and
15/16 height. Its sand entity remains active through observation 10, clips to
y=77.875 and drops on observation 11, and leaves only the source removal. The
old proof rejection, corrected candidate, and repeats are retained at
`trace/out/matrix_falling_soul_sand_red_1/summary.md`,
`trace/out/matrix_falling_soul_sand_candidate_1/summary.md`, and
`trace/out/matrix_falling_soul_sand_repeat_3/summary.md`. All nine affected
falling rows pass at `trace/out/matrix_falling_soul_sand_family/summary.md`.
The parked oracle now matches Java and C across 66 updates and six exact
sand/gravel drops at `trace/out/test_falling_drop_shaped_supports.log`.

An enchanting table adds the distinct 3/4-height failed-placement boundary.
Sand remains active through observation 10, clips to y=77.75 and drops on
observation 11, and leaves only the source removal. The first candidate exposed
a second causal defect: the live item integrator treated the partial table as
a full cube and snapped the new item to y=78 instead of letting it rise from
the exact surface. The corrected candidate passes at
`trace/out/matrix_enchanting_table_hay_candidate_3/summary.md`, all nine
promotion repeats pass at
`trace/out/matrix_enchanting_table_hay_repeat_3/summary.md`, and all 44
combined falling/fire controls pass at
`trace/out/matrix_enchanting_table_hay_family/summary.md`. The parked Java
oracle now agrees with C on 88 updates and eight exact sand/gravel drops at
`trace/out/test_falling_drop_enchanting_table.log`. This fixture covers only
the collision/drop boundary; it does not claim general enchanting-table tile
state or enchanting behavior.

Hay and carpet now have explicit coverage for their shared Java fire-table row
of encouragement/flammability 60/20. A direct hay fixture proves flammability
20 with the exact eleven-draw `Random(36)` cursor. A netherrack-source volume
fixture uses `Random(391)` roll 3, which hay admits at threshold 3 while the
bookshelf row would reject it at threshold 2; it creates only the intended
age-zero child and preserves the exact due-time-sorted queue. The shared CPU
table asserts both ids 170 and 171 in 38 lines at
`trace/out/test_world_tick_hay_carpet_table_cpu.log`; CUDA execution remains
deferred while GPU 1 is shared.

The combined full native aggregate passes in 6:48.67 at 346,044 KB peak RSS,
zero major faults, zero swap, and exit zero at
`trace/out/test_runtime_enchanting_table_hay.log`, with timing metadata in the
adjacent `.time` file. After stopping both exact oracle clients, the clean
scalar guard passes at 4,671 steps/s against the frozen 4,062 baseline and
3,858.9 floor at
`trace/out/perf_guard_enchanting_table_hay_cpu_1.json`. GPU 1 stayed untouched,
so the promoted total remains 676 pending CUDA/Blaze evidence.

A stone-supported carpet adds the 1/16-height failed-placement boundary. Sand
remains active through update 12, clips to y=77.0625 and drops on update 13,
then follows the exact Java item Y/Y-velocity, health, age, and pickup delay
through update 15. The old proof fence rejects the scheduled callback at
`trace/out/matrix_falling_carpet_red_1/summary.md`; the corrected row passes at
`trace/out/matrix_falling_carpet_candidate_1/summary.md` and all three repeats
pass at `trace/out/matrix_falling_carpet_repeat_3/summary.md`. All five shaped
supports pass together at
`trace/out/matrix_falling_shaped_support_family/summary.md`, with every
behavior and raw-block gate green. The parked Java oracle agrees with C on 114
falling updates and ten exact sand/gravel drops at
`trace/out/test_falling_drop_carpet.log`. As with the enchanting table, the
live item integrator treats carpet as a partial surface so an upward-moving
new item is not snapped to the top of a full cube.

The shared fire-table probe now also locks the already-correct coal-block 5/5
row beside bookshelf, hay, and carpet. Its 39-line CPU output passes at
`trace/out/test_world_tick_carpet_coal_cpu.log`; this is drift coverage, not a
coal behavior correction. The combined native aggregate passes in 7:26.14 at
348,624 KB peak RSS, zero major faults, zero swap, and exit zero at
`trace/out/test_runtime_carpet.log`, with timing metadata in the adjacent
`.time` file. The ordinary and CPU-70 scalar captures are preserved at
`trace/out/perf_guard_falling_carpet_cpu_1.json` and
`trace/out/perf_guard_falling_carpet_cpu70_diagnostic_1.json`: an unrelated
64-thread self-play job plus backup/training work held the host near 81 load,
so both absolute samples are below the frozen floor despite the unchanged
trajectory hash. They are contaminated evidence, not a regression verdict.
GPU 1 remained shared and untouched; clean performance promotion is pending
and the promoted total remains 676.

Snow layers now cover both semantic endpoints and every metadata value in the
parked exact oracle. One-layer snow has a zero-height collision box and is
replaceable: sand remains active through update 12, lands at y=77 on update
13, replaces only the snow cell, creates no item, and drains the normal `+2`
stability callback. Eight-layer snow collides at 7/8 height, is
nonreplaceable, and takes the exact update-11 item-drop path while retaining
78:7. The proof-fence rejection is retained at
`trace/out/matrix_falling_snow_red_1/summary.md`; both corrected endpoints pass
at `trace/out/matrix_falling_snow_candidate_1/summary.md`, all six repeats pass
at `trace/out/matrix_falling_snow_repeat_3/summary.md`, and all seven shaped
supports retain green behavior/block gates at
`trace/out/matrix_falling_snow_shaped_family/summary.md`. The one-layer row is
strict with 26 matching state features; the item-drop rows retain only their
declared ambient cursor diagnostic.

The generalized parked Java command checks metadata 0..7 for both sand and
gravel. Java and C agree on 302 falling updates across 26 cases, including the
metadata-dependent collision planes, replacement versus item branch, support
state, Math cursor, and entity IDs, at
`trace/out/test_falling_drop_snow_layers.log`. The final native aggregate
passes in 5:46.00 at 350,056 KB peak RSS, zero major faults, zero swap, and
exit zero at `trace/out/test_runtime_snow_layers.log`. The clean scalar guard
passes at 4,645 steps/s against the frozen 4,062 baseline and 3,858.9 floor at
`trace/out/perf_guard_falling_snow_cpu_1.json`. Snow collision work is reached
only for an active falling/item entity and adds no idle scan or allocation.
GPU 1 stayed shared and untouched, so the promoted total remains 676 pending
CUDA/Blaze evidence.

Farmland and cake add three more exact failed-placement rows. Dry and fully
moist farmland share the 15/16 collision plane: sand drops on update 10,
leaves moisture metadata 0 or 7 unchanged, and does not trample the block
because `EntityFallingBlock` is not living. A whole centered cake uses its
1/2-height collision plane, drops on update 12, and remains 92:0. The old
runtime rejects both support classes at
`trace/out/matrix_falling_cake_farmland_red_1/summary.md`; all three corrected
rows pass at
`trace/out/matrix_falling_cake_farmland_candidate_1/summary.md`, all nine
repeats pass at
`trace/out/matrix_falling_cake_farmland_repeat_3/summary.md`, and the full
ten-support family retains green behavior/block gates at
`trace/out/matrix_falling_cake_farmland_shaped_family/summary.md`.

The parked oracle now covers both falling identities, every farmland moisture
value, every snow-layer value, cake, and the prior supports. Java and C agree
on 486 updates across 44 cases at
`trace/out/test_falling_drop_cake_farmland.log`. The final native aggregate
passes in 5:49.79 at 354,736 KB peak RSS, zero major faults, zero swap, and
exit zero at `trace/out/test_runtime_cake_farmland.log`. The scalar guard
passes at 5,088 steps/s against the 3,858.9 floor at
`trace/out/perf_guard_falling_cake_farmland_cpu_1.json`. Both branches are
active-entity-only, fixed-cost checks with no allocation or idle scan. GPU 1
stayed shared and untouched, so the promoted total remains 676 pending
CUDA/Blaze evidence.

The next W-01 falling-block candidate carries the global `doEntityDrops`
gamerule through the Java authoritative snapshot, canonical state capsule,
script replay, and C runtime. With the rule disabled, sand still consumes the
one falling-entity ID, follows the exact 11-state bottom-slab trajectory, and
retires on update 12, but it creates no item and consumes neither a second ID
nor any `Math.random` draw. The old branch fails only that no-item boundary at
`trace/out/matrix_falling_entitydrops_false_red_1/summary.md`; the corrected
strict row has 26 matching simulated features, exact source-only raw mutation,
and exact rule state at
`trace/out/matrix_falling_entitydrops_false_candidate_2/summary.md`. All three
strict repeats pass at
`trace/out/matrix_falling_entitydrops_false_repeat_3/summary.md`, and the
11-case shaped-support family keeps every behavior and block gate green at
`trace/out/matrix_falling_entitydrops_family/summary.md`.

The parked oracle now agrees on 510 falling updates across 46 sand/gravel
cases, including both disabled-rule identities, at
`trace/out/test_falling_drop_entitydrops_false.log`. The full native aggregate
passes in 6:35.42 at 355,964 KB peak RSS, zero major faults, zero swap, and
exit zero at `trace/out/test_runtime_entitydrops.log`. The clean scalar guard
passes at 4,487 steps/s against the 3,858.9 floor at
`trace/out/perf_guard_falling_entitydrops_cpu_1.json`. The added branch runs
only when an active falling entity reaches its failed-placement boundary and
adds no scan or allocation. This proof covers falling-block failed placement,
not every future mob or block-drop caller of the gamerule. GPU 1 stayed shared
and untouched, so the promoted total remains 676 pending CUDA/Blaze evidence.

The next W-01 falling-block seam implements Java's post-move timeout exactly
inside the active-entity loop. After `fallTime` increments, gravity, movement,
and 0.98 drag, the entity retires when `fallTime > 100` and its floored Y is
below 1 or above 256, or unconditionally after `fallTime > 600`. The enabled
`doEntityDrops` branch creates and ticks the ordinary sand item with the exact
four-call `Math.random` and entity-ID effects; the disabled branch retires
without either cursor change. A deliberate no-timeout build stays alive and
fails at `trace/out/test_falling_timeout_red.log`. The corrected parked oracle
matches Java across all six high/low/age and enabled/disabled cases at
`trace/out/test_falling_timeout.log`, including exact position, velocity, item
lifecycle, unchanged blocks, and causal cursors. Review added the initially
missing lower-world half of the symmetric height predicate and hardened Java
cleanup before acceptance.

The shared item-drop refactor retains all 510 prior falling updates and 46
support/drop cases at
`trace/out/test_falling_drop_timeout_refactor.log`. Native coverage also pins
the bounded full-item-table policy: an expired block retires instead of
becoming an immortal retry loop when its item cannot be represented. The
final aggregate passes in 5:33.15 at 359,180 KB peak RSS, zero major faults,
zero swap, and exit zero at
`trace/out/test_runtime_falling_timeout_2.log`. The scalar guard passes at
5,031 steps/s against the 3,858.9 floor at
`trace/out/perf_guard_falling_timeout_cpu_1.json`. The new work is reached only
when `falling_block_count` is nonzero and adds no idle scan or allocation. GPU
1 stayed shared and untouched, so the promoted total remains 676 pending
CUDA/Blaze evidence. Lateral swept motion and dynamic landing-cell selection
are covered by the next bounded falling seam.

The W-01 lateral falling-block slice now uses Entity's retained 0.98-by-0.98
absolute AABB and exact Y-X-Z swept resolution. It preserves Java's
`resetPositionToBB` rounding at large world coordinates, collision flags,
clipped-axis velocity zeroing, float `fallDistance`, gravity/drag order, and
dynamic landing-cell selection. A free diagonal sand entity lands four cells
east and two south of its source; an X-wall case clips to the exact
`origin + 0.5099999904632568` boundary and settles in its original column.
The parked real-Java comparator matches all 26 updates and final raw blocks at
`trace/out/test_falling_lateral_ordered_final.log` and
`trace/out/test_falling_lateral_ordered_repeat.log`. Its landing callback is
checked behind a same-time, same-priority predecessor, proving relative queue
order 0/1 as well as delay and priority. The Java fixture now rejects any
pre-existing work in its X/Z footprint instead of draining and renumbering
unrelated callbacks.

The same review corrected a stale native assumption about pressure plates.
Circuit-material plates have a null collision box but are not replaceable by
sand or gravel. All four plate IDs remain in place; wood/gold/iron activate to
metadata one, stone excludes the nonliving falling entity, and the resulting
item rises through the plate instead of snapping to a false full-cube top.
The expanded parked family passes 614 falling updates across 54 cases at
`trace/out/test_falling_drop_pressure_plate_final_3.log`; timeout remains
exact at `trace/out/test_falling_timeout_after_bbox.log`.

The full native aggregate passes in 4:59.15 at 359,648 KB peak RSS, zero
major faults, and zero swap at
`trace/out/test_runtime_falling_lateral_2.log` and
`trace/out/test_runtime_falling_lateral_2.time`. With the oracle stopped, the
scalar guard passes at 4,986 steps/s against the frozen 4,062 baseline and
3,858.9 floor at
`trace/out/perf_guard_falling_lateral_cpu_1.json`. Active falling entities
retain six AABB endpoints; the inactive hot path remains one count check with
no allocation or world scan. GPU 1 stayed shared and untouched, so the
promoted total remains 676 pending CUDA/Blaze evidence. Z-only/corner and
simultaneous vertical-horizontal shapes, partial-height lateral obstacles,
moving-piston collision, lateral failed-placement/capacity paths, anvils, and
dynamic fluid columns remain separate work.

The scheduled dragon-egg slice is now exact for ordinary loaded-world
placement, support loss, the supported no-fall callback, and a short
successful fall. `BlockDragonEgg.onBlockAdded` and `neighborChanged` both
queue `+5`; duplicate insertion preserves the original due time. At the due
boundary the supported case drains without a block, RNG, or entity-ID change.
The unsupported case creates one metadata-0 falling egg, removes the source on
entity update one, follows the shared 13-row trajectory, lands three cells
below the source, and re-enters ordinary block placement so the settled egg
queues one fresh `+5` callback.

The parked Java/C comparator passes all 13 falling rows plus the supported
negative at `trace/out/test_falling_dragon_egg_candidate_2.log`, with two more
independent passes at
`trace/out/test_falling_dragon_egg_repeat_2.log`. It compares the initial and
post-neighbor queues, every position/velocity/collision/fall-distance row,
the full fixture block volume, retired entity state, both RNG cursors, and the
entity-ID cursor. Capsule self-test now covers both a supported dragon-egg
callback and a proof-fenced falling callback at
`trace/out/state_capsule_dragon_egg_final_2.log`. A native-only fixed-pool
negative fills all 16 represented falling slots and proves that a due egg
callback drains while preserving the source and cursors at
`trace/out/test_falling_dragon_egg_capacity.log`; this is an explicit bounded
resource policy, not a Java allocation-parity claim.

The final native aggregate passes in 4:59.26 at 365,004 KB peak RSS, zero
major faults, and zero swap at
`trace/out/test_runtime_dragon_egg_final_2.log` and its adjacent `.time` file.
With the Java client stopped, scalar throughput passes at 5,081 steps/s
against the frozen 4,062 baseline and 3,858.9 floor at
`trace/out/perf_guard_falling_dragon_egg_cpu_1.json`. Scheduling is reached
only on placement, neighbor notification, or a due callback; entity work
remains behind the existing nonzero falling count. GPU 1 stayed untouched, so
the promoted total remains 676 pending CUDA/Blaze evidence. Click teleport,
the unloaded/`fallInstantly` branch, scheduled-table exhaustion, and broader
falling collision cases remain open.

Scheduled anvils now cover the bounded loaded-world falling and impact-state
slice. Placement and ordinary support loss queue one de-duplicated `+2`
callback. The entity carries canonical metadata 0..11, removes its source on
update one, follows the exact 13-row retained-AABB trajectory, and lands three
cells below with a fresh supported `+2` callback. `BlockAnvil.onStartFalling`
enables the impact branch, so the runtime now retains the entity-local
48-bit `Random` cursor and consumes exactly one `nextFloat` after the measured
2.902239-block pre-impact distance. The parked oracle selects both sides of
the threshold: a high roll preserves metas 0/1/4/8, while internal seed zero
advances 0 to 4 and makes damage-tier-2 meta 8 break without placing or
dropping an item.

Java and magma agree on all 78 updates and seven supported/fall cases at
`trace/out/test_falling_anvil_after_restore_fence.log`, after three identical
full repeats at `trace/out/test_falling_anvil_repeat_3.log`. The comparator
also pins source removal, final block volume, collision flags, float fall
distance, both global RNG cursors, the entity RNG cursor on every row, entity
ID, landing queue, and the native-only full-16-slot atomic policy. The
save-capsule proof admits only supported canonical anvil callbacks, because a
world save does not contain the clock-seeded `Entity.rand` needed by a future
fall; the falling form is rejected rather than guessed. That positive and
negative proof passes at `trace/out/state_capsule_anvil_1.log`.

Failed placement now covers the item-state boundary as well. Replacing the
center of the landing plane with a bottom stone slab produces the exact
14-update trajectory and 3.36419439 pre-impact distance. A controlled high
roll consumes the same single entity `nextFloat`, placement fails in the
occupied slab cell, and `BlockAnvil.damageDropped` discards horizontal facing:
input metas 0/1/4/8 create item metas 0/0/1/2. The item consumes one entity ID
and four `Math.random` doubles, and its constructor plus first tick agree in
position, motion, yaw, age, pickup delay, stack, and all cursor states. The
expanded comparator passes 134 updates and 11 cases twice at
`trace/out/test_falling_anvil_drop_1.log` and
`trace/out/test_falling_anvil_drop_repeat.log`.

The explicit process-global `BlockFalling.fallInstantly` mode now takes the
synchronous worldgen path. A due unsupported callback removes the source,
scans straight down through the admitted fall-through column, restores the
full blockstate above the first represented floor, and schedules the landed
block's normal callback. It creates no `EntityFallingBlock`, consumes no
entity ID or RNG, and preserves anvil metas 0/1/4/8. The public callback
restore accepts this bounded unsupported-anvil case without requiring a
future entity cursor. Java and magma agree across the expanded 15-case suite
in consecutive runs at `trace/out/test_falling_instant_final_1.log` and
`trace/out/test_falling_instant_final_2.log`; the public-restore rerun passes at
`trace/out/test_falling_instant_public_restore_repeat.log`. Native coverage
also runs metadata-0 sand through the same generic immediate path.

The final native aggregate including both anvil additions passes in 5:08.99 at
370,040 KB peak RSS, zero major faults, and zero swap at
`trace/out/test_runtime_falling_instant.log` and `.time`. Scalar throughput
passes at 5,103 steps/s against the 3,858.9 floor at
`trace/out/perf_guard_falling_instant_cpu_1.json`. The default false mode adds
no idle work; the bounded vertical scan runs only for a due falling callback
while the explicit worldgen switch is active.

The first bounded anvil target is now exact as well. A fresh, unarmored,
effect-free server player centered on the landing cell intersects the anvil's
retained AABB on update 13. Java and magma both derive impact two from the
2.902239-block pre-impact distance, apply four points of armorable ANVIL
damage, move health 20 to 16, set hurt resistance to 20 and hurt time to 10,
record last damage four, and add 0.1 food exhaustion. The source-less living
hit also consumes the exact two `Math.random` LCG steps used by Java's
`attackedAtYaw` `nextDouble` before the anvil consumes its own independent
`Entity.rand.nextFloat`.

The expanded Java/C comparator passes 147 falling updates and 16 cases in two
consecutive runs at `trace/out/test_falling_anvil_impact_final_1.log` and
`trace/out/test_falling_anvil_impact_final_2.log`. Its neighboring no-target,
damage-tier, break, failed-placement, and instant cases remain exact. The full
native aggregate passes in 5:02.51 at 372,576 KB peak RSS with zero major
faults and zero swap at `trace/out/test_runtime_anvil_impact.log` and `.time`.
With the Java client stopped, scalar throughput passes at 4,998 steps/s
against the frozen 4,062 baseline and 3,858.9 floor at
`trace/out/perf_guard_falling_anvil_impact_cpu_1.json`. Target work remains
behind both the nonzero falling-block branch and the impact predicate; there
is no new idle scan or allocation.

The adjacent hurt-immunity boundary is exact. Immediately before the same
landing update, the oracle injects Java's active 20/10 hurt window. Raw damage
four against prior raw four returns false without health, exhaustion, player
RNG, or `Math.random` changes. Against prior raw two it applies only the
two-point difference, moves health 20 to 18, records last damage four, and
adds 0.1 exhaustion while preserving the 20/10 timers. Both cases still
consume the falling anvil's one independent degradation `nextFloat` and keep
the same landing block and `+2` callback.

Java and magma pass 173 falling updates and 18 cases twice at
`trace/out/test_falling_anvil_immunity_final_1.log` and
`trace/out/test_falling_anvil_immunity_final_2.log`. The full native aggregate
passes in 5:22.12 at 378,148 KB peak RSS with zero major faults and zero swap
at `trace/out/test_runtime_anvil_immunity.log` and `.time`. With the Java
client stopped, scalar throughput passes at 5,089 steps/s at
`trace/out/perf_guard_falling_anvil_immunity_cpu_1.json`. The only product
change is correct RNG branching at an already-active player impact.

The fully absorbed fresh-hit boundary is exact. With four absorption points,
the same raw-four anvil hit is accepted, consumes absorption to zero, opens the
20/10 hurt window, and records last damage four, but leaves health at 20 and
food exhaustion at zero. Java still runs the source-less fresh-hurt direction
branch and consumes one `Math.random()` `nextDouble`; magma now distinguishes
that accepted zero-residual result from an immunity rejection.

The expanded comparator passes 186 updates and 19 cases in consecutive runs at
`trace/out/test_falling_anvil_absorption_final_1.log` and
`trace/out/test_falling_anvil_absorption_final_2.log`. The parked integrated
client shares static Entity-ID and `Math.random` cursors with the server, so
the fixture snapshots its Math cursor at the terminal impact boundary and
derives the controlled ID cursor from the exact falling/item fixture IDs;
later client-only observations remain diagnostic rather than contaminating
the proof. The full native aggregate passes in 5:16.55 at 375,032 KB peak RSS,
zero major faults, and zero swap at
`trace/out/test_runtime_anvil_absorption.log` and `.time`. The CPU product and
Java oracle build, and scalar throughput passes at 4,948 steps/s against the
4,062 baseline and 3,858.9 floor at
`trace/out/perf_guard_falling_anvil_absorption_cpu_1.json`.

The adjacent Resistance-I boundary is exact without a product change. Java's
amplifier-zero effect scales the post-armor raw four damage to the float value
3.2, leaving health at 16.8 and exhaustion at 0.1, while the hurt-immunity
bookkeeping deliberately retains raw last damage four and the fresh 20/10
timers. The same source-less `Math.random()` double and falling-entity
`nextFloat` ordering remains exact.

The Java/C comparator passes 199 updates and 20 cases in consecutive runs at
`trace/out/test_falling_anvil_resistance_final_1.log` and
`trace/out/test_falling_anvil_resistance_final_2.log`. The full native
aggregate passes in 5:04.39 at 380,944 KB peak RSS, zero major faults, and zero
swap at `trace/out/test_runtime_anvil_resistance.log` and `.time`. Java and the
CPU product build, and scalar throughput passes at 5,072 steps/s against the
4,062 baseline and 3,858.9 floor at
`trace/out/perf_guard_falling_anvil_resistance_cpu_1.json`. This promotion adds
only oracle/native coverage around the existing shared potion-damage kernel.

The first bounded non-player target is now exact for a controlled NoAI pig.
The pig starts centered in the retained landing AABB with health 10 and takes
the same raw-four ANVIL hit, ending at health 6 with raw last damage four.
Java consumes one pig `Entity.rand.nextDouble` for `setBeenAttacked`, one
global `Math.random()` `nextDouble` for the source-less hurt direction, two pig
`nextFloat` calls for hurt-sound pitch, then the falling entity's independent
degradation `nextFloat`. Magma preserves that target/global/falling RNG order
and scans the fixed mob slots only for an active anvil impact.

The cold oracle hook now samples magma immediately after the same falling-
entity phase as Java, so the single-pig 20/10 timers and every other field
compare exactly without a boundary waiver. The public runtime continues into
the later controlled-living phase and correctly stores 19/9; the native test
covers that post-tick behavior separately.

The adjacent ordered pair is exact for two freshly inserted NoAI pigs in one
chunk section. Java's target query returns chunk-section insertion order; the
bounded magma fixture obtains the same order from two fresh ascending slots.
Distinct target seeds prove A then B rather than merely two equal outcomes.
Both pigs end at health 6 with immediate timers 20/10, each target RNG advances
four LCG steps, constructor plus impact global Math advances 16 steps, and the
falling RNG advances once. The comparator also pins the ordered target IDs,
world RNG, trajectory, landing, and schedule.

The 22-case Java/C suite passes 225 updates exactly in consecutive runs at
`trace/out/test_falling_anvil_pigs_final_1.log` and
`trace/out/test_falling_anvil_pigs_final_2.log`. The full native aggregate
passes in 5:22.90 at 385,692 KB peak RSS with zero major faults at
`trace/out/test_runtime_anvil_pigs.log` and `.time`. Java and the CPU product
build, and scalar throughput passes at 5,069 steps/s against the 4,062
baseline and 3,858.9 floor at
`trace/out/perf_guard_falling_anvil_pigs_cpu_1.json`. This does not generalize
fresh-slot order to arbitrary slot reuse or natural-world entity ordering.

A controlled NoAI cow is now exact as the second represented living type.
Cow inherits the same fresh `EntityAnimal` damage path: health 10 becomes 6,
immediate hurt timers are 20/10, raw last damage is four, target RNG advances
four LCG steps, constructor plus impact global Math advances eight, falling
RNG advances once, and world RNG is unchanged. The target-query membership,
logical ID, trajectory, landing, and schedule are exact at the same immediate
phase boundary. The product predicate remains restricted to pigs and cows.

The 23-case Java/C suite passes 238 updates exactly in consecutive runs at
`trace/out/test_falling_anvil_cow_final_1.log` and
`trace/out/test_falling_anvil_cow_final_2.log`. The native aggregate, including
an ordered pig/pig/cow public-tick fixture, passes in 5:18.17 at 385,956 KB
peak RSS with zero major faults at `trace/out/test_runtime_anvil_cow.log` and
`.time`. Java and the CPU product build, and scalar throughput passes at 5,052
steps/s against the 4,062 baseline and 3,858.9 floor at
`trace/out/perf_guard_falling_anvil_cow_cpu_1.json`.

The first bounded armored-player target is exact for one plain, undamaged
diamond chestplate in survival mode. Raw anvil damage four reduces through
armor eight and toughness two to the exact float health
17.02400016784668 (`0x41883127`), damages chest slot 38 from durability zero
to one, opens the 20/10 hurt window, records raw last damage four, and adds
0.1 exhaustion. No other armor slot changes.

This promotion also closes the previously unobserved player-local random
cursor for every represented player impact. A fresh accepted hit consumes
the player's `Entity.rand.nextDouble` in `setBeenAttacked` and two
`nextFloat` calls for hurt-sound pitch, or four LCG steps in total. Fully
absorbed, Resistance-I, and armored fresh hits do the same; equal rejection
and stronger damage inside the active immunity window leave the cursor
unchanged. The source-less global `Math.random` double and falling-entity
degradation draw remain separately pinned.

The 24-case Java/C suite passes 251 updates exactly in consecutive runs at
`trace/out/test_falling_anvil_armor_final_1.log` and
`trace/out/test_falling_anvil_armor_final_2.log`. The native aggregate passes
in 5:02.36 at 387,440 KB peak RSS with zero major faults and zero swap at
`trace/out/test_runtime_anvil_armor.log` and `.time`. Java and the CPU product
build, and scalar throughput passes at 5,143 steps/s against the 4,062
baseline and 3,858.9 floor at
`trace/out/perf_guard_falling_anvil_armor_cpu_1.json`. The added work is
impact-only; there is no idle scan or allocation. Enchantments, multiple
pieces, creative durability immunity, and generalized ANVIL source flags
remain separate slices.

The adjacent plain diamond-helmet target now closes the anvil-specific
head-slot pre-hook. Before hurt immunity and ordinary armor, Java consumes one
player `Entity.rand.nextFloat`, applies `int(16 + nextFloat * 8)` durability,
and scales raw four to three. With the pinned seed this is 19 special
durability; ordinary armor adds one more. The surviving slot-39 helmet ends at
durability 20, `lastDamage` is three, and helmet armor three/toughness two
leaves exact health 17.215999603271484 (`0x4189ba5e`). The complete player
cursor advances five LCG steps: one pre-hook draw, one `nextDouble`, and two
hurt-pitch `nextFloat` calls.

The 25-case Java/C suite passes 264 updates in consecutive runs at
`trace/out/test_falling_anvil_helmet_final_1.log` and
`trace/out/test_falling_anvil_helmet_final_2.log`. The native aggregate passes
in 4:59.85 at 393,744 KB peak RSS with zero major faults and zero swap at
`trace/out/test_runtime_anvil_helmet.log` and `.time`. Java and the CPU product
build, and scalar throughput passes at 5,151 steps/s at
`trace/out/perf_guard_falling_anvil_helmet_cpu_1.json`. The hook performs one
head-slot operation only after an intersecting anvil impact and adds no idle
work. Unbreaking, near-break/removal and item-break effects remain open.

A controlled NoAI sheep is now exact at the same immediate anvil-impact
boundary. This slice corrects two earlier generic-passive approximations:
vanilla sheep max health is eight, not ten, and its collision box is 0.9 by
1.3 blocks rather than the cow's 0.9 by 1.4. The oracle exports the actual
target AABB, so the centered impact cannot conceal that geometry correction.
Raw damage four leaves sheep health four, opens the 20/10 hurt window, and
records last damage four. The pinned target RNG advances four LCG steps, the
constructor plus source-less direction advance global Math eight steps,
falling RNG advances once, and world RNG is unchanged.

The 26-case Java/C suite passes 277 updates exactly in consecutive runs at
`trace/out/test_falling_anvil_sheep_final_1.log` and
`trace/out/test_falling_anvil_sheep_final_2.log`. The native aggregate extends
the ordered public-tick fixture to pig/pig/cow/sheep and passes in 5:03.68 at
252,404 KB peak RSS with zero major faults and zero swap at
`trace/out/test_runtime_anvil_sheep.log`. Java and the CPU product build, and
the stopped-oracle scalar guard passes at 4,916 steps/s against the 4,062
baseline and 3,858.9 floor at
`trace/out/perf_guard_falling_anvil_sheep_cpu_1.json`. The shared size constant
also serves CUDA builds, but GPU 1 stayed shared and untouched, so the promoted
total remains 676 pending CUDA/Blaze evidence. The subsequent chicken slice
below isolates its no-loot death boundary; hostiles, natural AI,
equipment/effects, slot reuse, and general target ordering remain open.

The controlled NoAI chicken now covers the first lethal passive impact
boundary. Vanilla chicken max health is four and its actual AABB is 0.4 by
0.7, both exported and compared. The fixture temporarily saves and disables
`doMobLoot`, because `EntityLivingBase.onDeath` runs loot immediately rather
than at death tick 20. At the exact landing return, health is zero,
`EntityLivingBase.dead` is true, `Entity.isDead` is false, `deathTime` is zero,
hurt state is 20/10 with last damage four, and no ItemEntity or XP allocation
exists. Target RNG advances four LCG steps for `setBeenAttacked` and death
sound pitch; constructor plus source-less direction advance global Math eight,
falling RNG advances once, and world RNG is unchanged.

The first native run exposed a later public-order divergence: the ordinary
mobs-enabled path aged chicken hurt timers but did not advance the controlled
20-tick death clock. The shared entity loop now performs the zero-health
controlled death update before AI/travel, so falling-first insertion order
leaves the immediate oracle at `deathTime=0` and the same public runtime tick
at `deathTime=1`. The ordered native fixture now covers
pig/pig/cow/sheep/chicken and all five RNG streams.

The current-source 27-case Java/C suite passes 290 updates exactly in
consecutive runs at `trace/out/test_falling_anvil_chicken_final_3.log` and
`trace/out/test_falling_anvil_chicken_final_4.log`. The corrected full native
aggregate passes in 5:05.39 at 252,668 KB peak RSS with zero major faults and
zero swap at `trace/out/test_runtime_anvil_chicken_final.log`. Java and the
CPU product build, and scalar throughput passes at 5,157 steps/s against the
4,062 baseline and 3,858.9 floor at
`trace/out/perf_guard_falling_anvil_chicken_cpu_1.json`. The death branch is
inside the already-active controlled living slot loop; it adds no scan or
allocation when mobs are absent. GPU 1 stayed shared and untouched, so the
promoted total remains 676 pending CUDA/Blaze evidence. Loot-enabled chicken
drops, exact EntityItem RNG/IDs, death sound events, particles, and terminal
removal remain separate slices.

The normal `doMobLoot=true` boundary is now exact for one pinned lethal
chicken roll. The 1.11.2 loot table has a feather pool with count zero through
two followed by one raw-chicken pool; the pinned target cursor produces two
feathers, then one raw chicken, as two ordered `EntityItem` stacks. The target
RNG advances seven LCG steps, global Math advances 24 (six for the chicken
constructor, two for null-source direction, and eight for each item
constructor), falling RNG advances once, world RNG is unchanged, and the
logical ID cursor advances four through falling/chicken/feather/chicken. Exact
item position, motion, yaw, hover phase, age, pickup delay, health, lifespan,
ground/dead state, and one subsequent public tick are compared.

An integrated-client oracle artifact initially shifted the shared static Math
cursor: constructing a client mirror chicken consumed six extra draws before
the server made its drops. The fixture now inserts this controlled target into
the server entity lists without client tracking, while a `LivingDrops` capture
retains the real server-constructed items. Two fresh-JVM 28-case runs pass all
303 updates at `trace/out/test_falling_anvil_chicken_loot_final_1.log` and
`trace/out/test_falling_anvil_chicken_loot_final_2.log`. The Java and product
builds pass. The native aggregate, including an atomic insufficient-item-slot
negative control, passes in 4:25.85 at 253,480 KB peak RSS, zero major faults,
and zero swap at
`trace/out/test_runtime_anvil_chicken_loot_final.log`. The stopped-oracle CPU
guard passes at 5,180 steps/s against the 4,062 baseline and 3,858.9 floor at
`trace/out/perf_guard_falling_anvil_chicken_loot_cpu_1.json`. The capacity
preflight is an explicit native safety policy: when all required drop slots
are unavailable, it rejects the target without partial health, RNG, ID, or
item mutation. GPU 1 remained untouched, so the promoted total stays 676.
Looting/killer/recent-hit XP, other mobs, emitted events/particles, terminal
removal, and snapshot persistence of exact item yaw/hover remain open.

All three feather cardinalities and the burning-meat condition now close the
adjacent loot-table surface. Pinned target cursors produce feather counts zero,
one, and two while retaining the same seven target LCG steps. Zero feathers
allocates only the meat stack, so global Math advances 16 total steps and IDs
advance three; one/two feathers allocate feather plus meat stacks, so global
Math advances 24 and IDs advance four. A fresh non-burning Java chicken exposes
internal fire `-1`; the burning fixture pins fire 100. The loot table's
`EntityOnFire` furnace-smelt condition changes only meat item 365 to cooked 366
and consumes no RNG.

Two fresh-JVM 31-case gates pass all 342 updates at
`trace/out/test_falling_anvil_chicken_cooked_final_1.log` and
`trace/out/test_falling_anvil_chicken_cooked_final_2.log`. The native aggregate
passes in 5:02.42 at 253,492 KB peak RSS, zero major faults, and zero swap at
`trace/out/test_runtime_anvil_chicken_cooked_final.log`. Java and the product
build. The stopped-oracle scalar guard passes at 5,088 steps/s against the
4,062 baseline and 3,858.9 floor at
`trace/out/perf_guard_falling_anvil_chicken_cooked_cpu_1.json`. The branch is
impact-only, GPU 1 remained untouched, and the promoted total stays 676 pending
CUDA/Blaze evidence.

The cooked chicken fixture now continues through the complete 20-tick death
lifecycle in the real parked Java world and the product runtime. Ticks one
through 19 retain the post-loot target cursor while death time advances, hurt
timers age, fire falls from 99 to 81, and both loot entities age with pickup
delay ten to zero. Tick 20 sets `Entity.isDead`, removes the chicken from the
loaded set, leaves zero XP for the null-attacker death, and reaches fire 80.
The terminal 20-particle loop consumes the exact target-local RNG stream:
three `nextGaussian` and three `nextFloat` calls per particle. Gaussian cache
reuse and rejection make this seed consume 200 LCG steps, not a constant 180
or 220; the complete impact-through-terminal cursor is 207 steps and ends at
`0x87abf0c5165a`. The exact emitted payloads are closed by the later observed
particle slice below; rendering those payloads remains separate pixel work.

The expanded 31-case gate passes 362 exact update rows in three fresh JVMs at
`trace/out/test_falling_anvil_chicken_post_final_1.log` through
`trace/out/test_falling_anvil_chicken_post_final_3.log`, including a final-source
run after ordering cleanup. The full native aggregate passes in 5:43.15 at
401,080 KB peak RSS, zero major faults, and zero swap at
`trace/out/test_runtime_anvil_chicken_post_final.log`. Java and the CPU product
build. The stopped-oracle scalar guard passes at 5,059 steps/s against the
4,062 baseline and 3,858.9 floor at
`trace/out/perf_guard_falling_anvil_chicken_post_cpu_1.json`. The added work is
dead-controlled-target-only, GPU 1 remained untouched, and the promoted total
stays 676 pending CUDA/Blaze evidence.

The immediate controlled-chicken impact event boundary is now observed rather
than inferred. Two common server mixins record `WorldServer.setEntityState`
and the final `ServerWorldEventHandler` sound payload after Forge substitution.
Every lethal chicken mode requires the exact ordered stream: status 2, the
namespaced `minecraft:entity.chicken.death` sound in category `neutral`, then
status 3 after synchronous loot. Position, volume one, and target-RNG-derived
float pitch are bit-compared. A fresh nonlethal native control emits status 2
and the hurt sound only; a lethal hurt-resistance delta emits status 3 without
replaying fresh-hit RNG.

The product stores this causal surface in a fixed 285-record ring, enough for
three records from every represented living slot. Sequence numbers are
monotonic and an explicit dropped counter exposes overwrite. Native tests fill
the ring exactly, append one terminal status, and require oldest sequence one,
last sequence 285, and one dropped record. Insufficient loot capacity remains
fully atomic and emits no partial status or sound record. Producers append only
when an event occurs, with no idle scan or heap allocation.

Two uncontaminated fresh-JVM 31-case gates pass all 362 update rows and the
new event payloads at
`trace/out/test_falling_anvil_chicken_events_final_1.log` and
`trace/out/test_falling_anvil_chicken_events_final_3.log`. A second attempt was
discarded after the already-known integrated-client global-Math race appeared
in the unrelated sheep fixture. The final native aggregate passes in 5:02.85
at 253,536 KB peak RSS with zero major faults and zero swap at
`trace/out/test_runtime_anvil_chicken_events_final.log`. Java and the CPU
product build. The stopped-oracle scalar guard passes at 5,169 steps/s against
the frozen 4,062 baseline and 3,858.9 floor at
`trace/out/perf_guard_falling_anvil_chicken_events_cpu_1.json`. GPU 1 remained
untouched, so the promoted total stays 676 pending CUDA/Blaze evidence.
Other species' sounds and statuses and the broader audio/render consumers
remain open.

The same observed event surface now covers every represented controlled
passive and exact multi-target ordering. Pig, cow, and sheep fresh nonlethal
hits emit status 2 followed immediately by their namespaced hurt sound. Two
overlapping pigs retain insertion order as
`status2/pig1 sound/status2/pig2 sound`, with distinct logical EIDs and
seed-derived float pitches despite identical positions. Cow's source override
uses volume 0.4; pig and sheep use volume one. All remain category `neutral`.

An `Entity.playSound` source-context mixin carries identity through the nested
World/Forge path, while the existing server-handler hook still records the
final substituted packet payload. This avoids position-based attribution and
adds no work when no oracle fixture is armed. The C product uses the same
event ring and appends only after an accepted controlled impact. Equal-damage
hurt-resistance rejection leaves event and RNG cursors unchanged.

Two fresh-JVM full gates pass all 362 rows and 31 cases at
`trace/out/test_falling_anvil_passive_events_final_1.log` and
`trace/out/test_falling_anvil_passive_events_final_2.log`. The full native
aggregate passes in 5:03.90 at 252,700 KB peak RSS, zero major faults, and zero
swap at `trace/out/test_runtime_anvil_passive_events_final.log`. Java and the
CPU product build. The stopped-oracle scalar guard passes at 5,037 steps/s
against the 4,062 baseline and 3,858.9 floor at
`trace/out/perf_guard_falling_anvil_passive_events_cpu_1.json`. GPU 1 remained
untouched and the promoted total stays 676 pending CUDA/Blaze evidence. Event
consumers, hostile mobs, breeding/child pitch, silent entities, and terminal
particles remain open.

Anvil landing and break `World.playEvent` payloads are now observed and
matched. A temporary Java `IWorldEventListener` records the true synchronous
server callback while the falling fixture is parked. Successful placement
emits exactly event 1031 after the anvil state is installed; terminal
damage-tier break emits exactly 1029 without installing a landing block. Both
use the integer landing cell and data zero. Supported, instant, and failed
placement/drop controls emit no such event.

The C product uses a separate allocation-free 16-record runtime world-event
ring, bounded by the fixed falling-entity pool. It carries monotonic sequence
and overwrite counters and appends only in the active terminal anvil branch;
the empty falling path remains the same single count check. The six focused
fall variants pass 78 exact updates at
`trace/out/test_falling_anvil_world_events_focus.log`, and the supported,
drop, and instant negative controls pass at
`trace/out/test_falling_anvil_world_events_negatives.log`.

Two fresh-JVM full gates pass all 362 rows and 31 cases at
`trace/out/test_falling_anvil_world_events_final_1.log` and
`trace/out/test_falling_anvil_world_events_final_2.log`. The complete native
runtime family passes in 6:09.52 at 405,236 KB peak RSS, zero major faults,
and zero swap at `trace/out/test_runtime_anvil_world_events_final.log`. Java
and the CPU product build. The stopped-oracle scalar guard passes at 5,004
steps/s against the frozen 4,062 baseline and 3,858.9 floor at
`trace/out/perf_guard_falling_anvil_world_events_cpu_1.json`. GPU 1 remained
untouched and the promoted total stays 676 pending CUDA/Blaze evidence.

The controlled-chicken terminal particle payload is now observed and exact,
not merely represented by its RNG side effect. At death tick 20, the scoped
Java `IWorldEventListener` records exactly 20 `EXPLOSION_NORMAL` particles
(ID zero), `ignoreRange=true`, and no integer parameters. Each payload is
bit-compared as six raw double words. Vanilla consumes three Gaussian
velocities before three float position draws for every particle; width 0.4 and
height 0.7 remain float arithmetic until Java widens each offset to double.
Ticks one through 19 require zero emitted batches and tick 20 requires one
atomic batch.

Four velocity words initially differed by one to three ULP because host
`log` is not Java 8 `StrictMath.log`. The shared RNG now uses the fdlibm
evaluation order on both host and device. The focused payload case is exact,
and the independent `entity_random` Java/CPU oracle still matches all 17 raw
outputs. An `sm_120 --fmad=false` CUDA compile-only check passes; GPU execution
remains deferred while GPU 1 is shared.

Magma retains one fixed 20-particle batch per represented living slot in a
95-batch allocation-free ring. Sequence and overwrite counters are observable;
the native regression fills all 95 slots in one controlled tick, appends one
more terminal entity, and requires oldest sequence one, last sequence 95, and
one dropped batch. The producer runs only when a controlled dead entity reaches
death tick 20, adding no idle scan or heap allocation.

Two fresh-JVM full gates pass all 362 rows and 31 cases at
`trace/out/test_falling_anvil_terminal_particles_final_1.log` and
`trace/out/test_falling_anvil_terminal_particles_final_2.log`, both in 1:26
with about 49 MB harness RSS and zero swap. The complete native runtime family
passes in 6:28.65 at 433,092 KB peak RSS with zero swap at
`trace/out/test_runtime_terminal_particles.log`. Java and the CPU product
build. The stopped-oracle scalar guard passes at 5,115 steps/s against the
4,062 baseline and 3,858.9 floor at
`trace/out/perf_guard_falling_anvil_terminal_particles_cpu_1.json`. GPU 1 was
not executed and the promoted total remains 676 pending CUDA/Blaze evidence.

The first player-credit terminal XP slice is now exact for an adult controlled
chicken. A saved `recentlyHit=20` plus the fixture server player survives to
death tick 20 with entry value one, so `EntityAnimal.getExperiencePoints`
consumes one `World.rand.nextInt(3)`, returns three for the pinned cursor, and
creates one unsplit `EntityXPOrb` before terminal removal and particles. The
orb consumes four `Math.random` doubles in yaw/X/Y/Z order, receives logical
ID 520004, and completes its same-world-tick gravity, move, and drag update.
Value, private health, age, pickup delay, color/target-color, yaw bits, all six
position/motion double words, world/Math cursors, and next entity ID are exact.
The `recentlyHit=19` control expires one tick earlier and proves zero XP plus
zero global cursor or ID consumption.

The product stores combat credit per represented living slot and preflights
the fixed 95-orb pool before any RNG or ID mutation. Native coverage proves
the positive boundary, expired-credit negative, and full-pool atomic
rejection. Oracle passives now enter the real server chunk/entity lists
without integrated-client tracker mirrors; the XP fixture is outside the
160-block tracking radius with its complete falling area explicitly loaded.
This removes the old cross-case client-constructor Math.random race rather
than hiding it with retries. The isolated launcher also skips ForgeGradle's
legacy `getAssetIndex` network task in offline mode.

Two consecutive full gates pass all 428 rows and 33 cases at
`trace/out/test_falling_anvil_chicken_xp_final_1.log` and
`trace/out/test_falling_anvil_chicken_xp_final_2.log`, each in about 1:36 with
about 49 MB harness RSS and zero swap. The full native family passes in
5:58.13 at 436,636 KB peak RSS with zero swap at
`trace/out/test_runtime_chicken_xp.log`. Java and the product build, and an
`sm_120 --fmad=false` CUDA compile-only probe of the shared orb structure
passes. The stopped-oracle scalar guard passes at 4,397 steps/s against the
4,062 baseline and 3,858.9 floor at
`trace/out/perf_guard_falling_anvil_chicken_xp_cpu_1.json`. GPU 1 was not
executed, so the promoted total remains 676 pending CUDA/Blaze evidence.

The same complete death consequence is now exact for a second living type: an
adult unsaddled pig. A fresh lethal hit on health four with `doMobLoot=true`
consumes the pig loot table's one-entry choice and `set_count` draw, producing
one raw-porkchop item entity (319) whose stack count is three for the pinned
target cursor. The immediate boundary has exact ordered status/death-sound/
status events, one item constructor, target RNG after six LCG steps, global
Math after 16 steps, and causal IDs through 520002. Player credit then survives
all 20 death ticks. At tick 20 the adult animal returns XP three, emits one
same-tick-updated orb at ID 520003, produces the exact 0.9 by 0.9 pig terminal
particle payload, and leaves world/Math/ID cursors exact.

The product pig branch uses the same per-target transactional preflight as the
chicken branch. One stacked item requires one fixed item slot regardless of
its count; insufficient capacity rejects that target before damage, event,
RNG, or ID mutation. Native tests prove the exact positive stack/cursors and a
completely full item-pool rejection. The Java fixture now inserts captured
loot into the real server entity list without a client tracker mirror and
directly advances the isolated real item/orb `onUpdate` methods. This removed
two measurement races without changing the game transition being compared.

Three consecutive focused pig gates pass in about nine seconds each. Two
consecutive full gates pass all 461 updates and 34 cases at
`trace/out/test_falling_anvil_pig_loot_xp_final_1.log` and
`trace/out/test_falling_anvil_pig_loot_xp_final_2.log`, each in about 1:42 with
46,120 KB peak harness RSS and zero swap. The full native family passes in
6:07.52 at 437,744 KB peak RSS with zero swap at
`trace/out/test_runtime_pig_loot_xp.log`. Java and the CPU product build. The
stopped-oracle scalar guard passes at 4,713 steps/s against the 4,062 baseline
and 3,858.9 floor at
`trace/out/perf_guard_falling_anvil_pig_loot_xp_cpu_1.json`. The new work is
active lethal-pig-only, GPU 1 remained untouched, and the promoted total stays
676 pending CUDA/Blaze evidence.

The same complete adult-animal boundary is now exact for a controlled cow.
Vanilla's two ordered fixed-roll pools consume a one-entry choice and
`set_count` draw apiece. For the pinned target cursor they produce one leather
stack (334 count one), followed by one raw-beef stack (363 count one), with
causal IDs 520002 and 520003. The fresh lethal target consumes eight LCG
steps, both item constructors leave global Math after 24 steps, and the exact
status/cow-death-sound/status event stream retains cow's volume 0.4 override.
The source-backed cooked-beef branch (364) is also locked natively.

Player credit survives the same 20-tick death lifecycle. Tick 20 returns XP
three, creates one same-tick-updated orb at ID 520004, emits the exact 0.9 by
1.4 cow particle payload, and retires the cow at final ID 520005. The pinned
particle Gaussians include rejection draws, so the verified terminal target
cursor is the initial seed advanced 216 LCG steps rather than an assumed
fixed per-particle count. World, Math, item ages, timers, credit, and every raw
orb and particle word match the real Java game.

Cow loot retains the product's explicit per-target capacity rule. This seed
requires two fixed item slots; a one-free-slot fixture rejects before health,
event, RNG, ID, or drop mutation. Native coverage locks raw and cooked
outcomes, pool order, counts, cursor movement, and atomic rejection. Three
consecutive focused gates pass in about 8.7 seconds each. Two consecutive full
gates pass all 494 updates and 35 cases at
`trace/out/test_falling_anvil_cow_loot_xp_full_1.log` and
`trace/out/test_falling_anvil_cow_loot_xp_full_2.log`, each in about 1:45 with
49,000 KB peak harness RSS and zero swap. The complete native wrapper passes in
5:39.50 at 440,644 KB peak RSS with zero swap at
`trace/out/test_runtime_cow_loot_xp_full.log`; Java and the CPU product build.
The stopped-oracle scalar guard passes at 4,520 steps/s against the 4,062
baseline and 3,858.9 floor at
`trace/out/perf_guard_falling_anvil_cow_loot_xp_cpu_1.json`, despite unrelated
host training load. The branch runs only for an accepted lethal controlled cow
and adds no idle scan or allocation. GPU 1 remained untouched, so the promoted
total stays 676 pending CUDA/Blaze evidence.

Adult sheep now retain Minecraft's complete five-bit fleece state: color
metadata 0..15 in the low nibble and the sheared flag in bit 4. It resets on
slot reuse, reaches the live renderer, and controls the loot table. Unsheared
red follows the same four-draw nested structure as white, emitting wool 35:14
count one at ID 520002 and raw mutton 423 count two at ID 520003. Its immediate
target cursor remains initial plus eight and both item constructors leave
global Math at step 24. Sheared red bypasses the color table, emits only raw
mutton 423 count two at ID 520002, leaves the target at initial plus six and
Math at step 16, and needs only one fixed item slot. The exact status/sheep-
death-sound/status stream and 0.9 by 1.3 geometry remain unchanged.

The 20-tick credited death sequence produces XP three and one same-tick-updated
orb. Unsheared colors retain ID 520004, final ID 520005, and terminal target
cursor plus 224. Sheared sheep use orb ID 520003, final ID 520004, and terminal
target cursor plus 222 because the particle path itself consumes the same 216
LCG steps after the shorter loot prefix. Every item age, timer, credit field,
World/Math cursor, and raw orb/particle word is exact. The source-backed
burning branch emits cooked mutton 424. Native capacity coverage proves an
unsheared two-slot target rejects a one-free-slot table atomically while the
same sheared target succeeds using that slot.

Focused white, red, sheared, and nonlethal sheep comparisons pass. Two
consecutive expanded full gates pass all 593 updates and 38 cases in 2:02.36
and 2:00.64 at `trace/out/test_falling_anvil_sheep_state_full_1.log` and
`trace/out/test_falling_anvil_sheep_state_full_2.log`, with about 46 MB peak
harness RSS and zero swap. Java, the C product, and the entity-render suite
pass. The complete native aggregate passes in 5:17.19 at 253,716 KB peak RSS,
zero major faults, and zero swap at
`trace/out/test_runtime_sheep_state_full.log`. With the oracle stopped, the
scalar guard passes at 4,664 steps/s against the 4,062 baseline and 3,858.9
floor at `trace/out/perf_guard_falling_anvil_sheep_state_cpu_1.json`. The
branch is accepted-lethal-only and adds no idle scan or allocation. GPU 1
remained untouched, so the promoted total stays 676 pending CUDA/Blaze
evidence. General sheep save-capsule persistence remains open because the
current versioned entity capsule intentionally admits only its locked NoAI pig
fixture.

The prior falling families remain green after the shared impact-distance
change: 13 dragon-egg, 26 lateral, six timeout, and 614 shaped/drop updates
pass at the corresponding `trace/out/*_after_anvil.log` files. The final
native aggregate with the item mapping passes in 6:07.55 at 370,148 KB peak
RSS, zero major faults, and zero swap at
`trace/out/test_runtime_anvil_drop.log` and `.time`. Scalar throughput passes
at 5,183 steps/s against the frozen 4,062 baseline and 3,858.9 floor at
`trace/out/perf_guard_falling_anvil_drop_cpu_1.json`. The extra metadata branch
runs only when an active anvil actually creates an item; there is no idle scan
or allocation. GPU 1 stayed untouched, so the promoted total remains 676
pending CUDA/Blaze evidence. General impact damage beyond the represented
player and controlled sheep/pig/cow/chicken slices, including other living types,
enchanted/multi-piece armor, other effects, and general multi-target ordering,
remains broader work. World-event audio/particle consumers, item-pool
pressure, moving-piston/fluid interactions, the
area-not-loaded predicate, dynamic fall-through columns, and broader collision
cases remain open.

The next W-01 candidate adds the outer `doFireTick` gamerule guard to both
controlled and scheduled fire callbacks. A real scheduled callback remains
pending for observations 0-1, drains on observation 2, leaves source fire and
the east plank unchanged, and creates no successor. Before the fix, the Java
queue drained while the capsule omitted the callback at
`trace/out/matrix_fire_tick_disabled_prefix_3/summary.md`. The corrected
disabled row is strict with 26 matching state features and all 10,625 block
cells unchanged at
`trace/out/matrix_fire_tick_disabled_candidate/summary.md`. The paired enabled
scheduled control still burns the east plank and creates its two +35 entries;
both rows pass at `trace/out/matrix_fire_rule_pair/summary.md`, and the enabled
direct-callback control remains strict at
`trace/out/matrix_fire_callback_control/summary.md`. Three disabled repeats
pass at `trace/out/matrix_fire_tick_disabled_repeat_3/summary.md`, and the
exact-source clean-build replay passes at
`trace/out/matrix_fire_tick_disabled_clean_final/summary.md`. The full native
aggregate passes in 7:10.18 with a 306,748 KB peak and zero swap at
`trace/out/test_runtime_fire_disabled.log`. The guard is one callback-local
branch and adds no loaded-world scan or allocation. A scalar recapture under
an unrelated 64-thread TAK workload measured only 3,373 steps/s and is retained
as contaminated evidence at
`trace/out/perf_guard_fire_tick_disabled_cpu_1.json`; the immediately preceding
clean 4,230 steps/s floor remains the current comparable result until that
workload retires. GPU 1 was shared and untouched, so the promoted total remains
676.

The adjacent W-01 fire proof now covers three more callback branches. An
age-four fire on ordinary stone with no flammable neighbor consumes
`nextInt(3)` and `nextInt(10)`, schedules its stale successor, then burns out
from the original age. The strict row passes in the combined family at
`trace/out/matrix_fire_eternal_source_family/summary.md`. Netherrack in the
Overworld and Nether plus End bedrock now exercise every admitted
dimension/source pair for vanilla's represented infinite-source rules.
Age-15 source fire survives unchanged, consumes the exact seven-draw direct
callback sequence, and preserves one source schedule. The netherrack scheduled
prefix omitted the queue while raw blocks happened to remain equal at
`trace/out/matrix_fire_netherrack_source_prefix/summary.md`; both scheduled and
parked direct modes pass three repeats at
`trace/out/matrix_fire_netherrack_source_repeat_3/summary.md`. End scheduled
world observation has ambient cross-world RNG between the reset hook and
callback, so its strict oracle owns the parked direct callback while native
coverage owns delayed dispatch. Three exact End repeats pass over 22,869 cells
at
`trace/out/matrix_fire_end_bedrock_source_callback_repeat_3/summary.md`. The
seven-case affected-control family passes with 26 matching simulated features,
zero divergences, exact queues and controlled cursors, and exact raw blocks at
`trace/out/matrix_fire_eternal_source_family/summary.md`. The final native
aggregate passes in 6:31.14 with a 313,392 KB peak and zero swap at
`trace/out/test_runtime_fire_end_bedrock.log`. The clean scalar guard passes at
4,282 steps/s against the 4,062 baseline and 3,858.9 floor at
`trace/out/perf_guard_fire_eternal_sources_cpu_1.json`. The proof changes run
only for active fire callbacks and add no allocation or idle scan. GPU 1 was
shared and untouched, so the promoted total remains 676.

Three more strict R-02 candidates add live redstone power to doors, fence
gates, and trapdoors. The one-tick old-C probes place one redstone block and
leave all 26 simulated state features exact while missing only two door cells,
one gate cell, or one trapdoor cell at
`trace/out/matrix_redstone_power_wooden_access_probe_1/summary.md`. The
corrected door changes lower/upper metadata `1/8` to `5/10`, the gate changes
`0` to `12`, and the trapdoor changes `0` to `4` at
`trace/out/matrix_redstone_power_wooden_access_candidate_1/summary.md`.
Native controls cover all seven door IDs, six gate IDs, both trapdoor IDs,
and exact source-removal closure. The three power-removal rows also pass Java
strict at
`trace/out/matrix_redstone_unpower_wooden_access_candidate_1/summary.md`, and
the neighboring random, physical-use, collision, and piston family passes
10/10 at
`trace/out/matrix_redstone_power_wooden_access_affected_1/summary.md`.
The later exact-source aggregate includes this family and passes at
`trace/out/test_runtime_redstone_daylight_detector_use_final.log`;
performance remains deferred on the shared GPU.

Portfolio snapshot: 15 of 32 ordered bundles are `DONE`, 16 are `ACTIVE`,
and one is `QUEUED`. Because the remaining villager AI, monument/mansion,
automation edge, audio, persistence, and pixel work is larger than many of the
finished harness items, the effort-weighted full-replica estimate is
approximately 70%, with high uncertainty. Oracle case count measures
regression depth, not global feature completion.
The table below retains each case's result when first promoted or first made
exact as a candidate; earlier rows therefore show preceding state schemas.

| Case | Inputs | State result when promoted | Raw block result |
|---|---:|---|---|
| random seed 0 | 60 ticks | 17 matches, zero divergences, 1 explicit unsupported | exact full-runtime transition |
| random seed 1 | 60 ticks | 17 matches, zero divergences, 1 explicit unsupported | exact full-runtime transition |
| break stone seed 0 | 180 ticks | 17 matches, zero divergences, 1 explicit unsupported | stone-to-air transition exact and non-vacuous |
| drowning seed 0 | 340 ticks | 17 matches, zero divergences, 2 exact damage events | exact full-runtime transition |
| surface reset seed 0 | 80 ticks | 17 matches, zero divergences, air resets on tick 38 | exact full-runtime transition |
| surface packet boundary seed 0 | 25 ticks | 17 matches, zero divergences, delayed server landing/jump exact | exact full-runtime transition |
| fire counter seed 0 | 45 ticks | 17 matches, zero divergences, signed countdown and damage cadence exact | exact full-runtime transition |
| wet fire extinguish seed 0 | 2 ticks | 17 matches, zero divergences, immediate extinguish without damage | exact full-runtime transition |
| fire block contact seed 0 | 20 ticks | 17 matches, zero divergences, contact damage/exhaustion/ignition exact | exact full-runtime transition |
| controlled fire spread callback seed 0 | 1 tick | 17 matches, zero divergences, exact 11-draw Java RNG sequence and two-entry queue | one east plank-to-age-0-fire mutation, 10,625/10,625 exact |
| controlled wool fire callback seed 0 | 1 tick | 26 matches, zero divergences, exact 11-draw Java RNG sequence and two-entry queue | one east wool-to-age-0-fire mutation, 10,625/10,625 exact |
| controlled log fire callback seed 0 | 1 tick | 26 matches, zero divergences, exact 11-draw Java RNG sequence and +31/+38 queue | one east log-to-age-0-fire mutation, 10,625/10,625 exact |
| fire activates valid X portal seed 0 | 1 tick | 26 matches, zero divergences, empty fire queue and zero controlled cursor draws | six air-to-portal-axis-X mutations, 10,625/10,625 exact |
| fire activates valid Z portal seed 0 | 1 tick | 26 matches, zero divergences, empty fire queue and zero controlled cursor draws | six air-to-portal-axis-Z mutations, 10,625/10,625 exact |
| fire activates legal height-16 X portal seed 0 | 1 tick | 26 matches, zero divergences, aligned bounded staging with empty queue and zero cursor draws | 32 air-to-portal-axis-X mutations, 11,250/11,250 exact |
| fire in broken X portal frame seed 0 | 1 tick | 26 matches, zero divergences, exact one-draw cursor and +39 fire queue | one air-to-fire mutation, 10,625/10,625 exact |
| scheduled fire spread seed 0 | 4 ticks | 17 matches, zero divergences, exact pending lifetime, due dispatch, and +35 child queue | one east plank-to-age-0-fire mutation, 10,625/10,625 exact |
| scheduled fire with `doFireTick=false` seed 0 | 4 ticks | 26 matches, zero divergences, exact pending lifetime, due drain, and no successor | zero mutations, 10,625/10,625 exact |
| age-4 isolated fire burnout seed 0 | 4 ticks | 26 matches, zero divergences, exact pending lifetime, two-draw callback, stale successor, and burnout | one age-4-fire-to-air mutation, 10,625/10,625 exact |
| age-15 netherrack source callback/schedule seed 0 | 1/4 ticks | 26 matches, zero divergences, exact seven-draw callback and persistent source queue | zero mutations, 10,625/10,625 exact |
| age-15 End-bedrock source callback seed 0 | 1 tick | 26 matches, zero divergences, exact seven-draw callback and persistent source queue | zero mutations, 22,869/22,869 exact |
| normal/humid direct TNT chance controls seeds 0/7 | 1 tick | 26 matches, zero divergences, exact 11/9-draw branch cursors and TNT entity state | normal makes zero mutations; humid replaces only TNT with air, 10,625/10,625 exact |
| normal/humid volumetric spread thresholds seeds 0/7 | 1 tick | 26 matches, zero divergences, exact roll-two versus threshold-two/one decision and child queue | normal makes first air candidate age-1 fire; humid makes zero mutations, 10,625/10,625 exact |
| rain-exposed/covered east tall-grass target seed 0 | 1 tick | 26 matches, zero divergences, exact wet-target short circuit and covered child queue | source ages 0-to-1; exposed target becomes air while covered target becomes age-0 fire, 10,625/10,625 exact |
| rain-exposed/covered west volumetric candidate seed 0 | 1 tick | 26 matches, zero divergences, exact successful roll, exposed suppression cursor, and covered child queue | exposed makes zero mutations; covered makes the sole air-to-age-0-fire mutation, 10,625/10,625 exact |
| XP pickup seed 0 | 20 ticks | 17 matches, zero divergences, orb motion/removal and XP award exact | exact full-runtime transition |
| melee cooldown seed 0 | 70 ticks | 17 matches, zero divergences, weak-hit rejection plus four full hits and death exact | exact full-runtime transition |
| Speed II expiry seed 0 | 12 ticks | 17 matches, zero divergences, duration and derived movement exact | exact full-runtime transition |
| inert scheduled stone seed 0 | 6 ticks | 17 matches, zero divergences, exact due/order/drain | zero mutations, exact full-runtime transition |
| water-source dispatch seed 0 | 6 ticks | 17 matches, zero divergences, exact source and five child updates | four level-1 mutations, 10,625/10,625 exact |
| flat-water descendants seed 0 | 8 ticks | 17 matches, zero divergences, exact 5-entry then 12-entry queues | static source plus level-1/2 rings, 13 mutations exact |
| downward-water descendants seed 0 | 8 ticks | 17 matches, zero divergences, exact 2-entry then 10-entry queues | metadata-8 falling column plus two level-1 rings, 9 mutations exact |
| lava-source dispatch seed 0 | 31 ticks | 17 matches, zero divergences, exact natural 30-tick cadence and five-child queue | four level-2 mutations exact |
| flat-lava descendants seed 0 | 61 ticks | 17 matches, zero divergences, exact 5-entry then 12-entry queues | still source plus level-2/4 rings, 13 mutations exact |
| downward lava into water seed 0 | 31 ticks | 17 matches, zero divergences, exact water drain/reaction/source requeue | one water-source-to-stone mutation exact |
| isolated natural cactus selection seed 0 | 3 ticks | 17 matches, zero divergences, exact loaded-order/`updateLCG` target with zero pre-advances | one cactus age-0-to-age-1 mutation, 10,625/10,625 exact |
| wheat random-tick callback seed 0 | 3 ticks | 17 matches, zero divergences, exact Java `Random(seed=9)` callback | one age-0-to-age-1 metadata mutation, 10,625/10,625 exact |
| live block-light addition seed 0 | 1 tick | 17 matches, zero divergences, real server-thread placement at tick 0 | one air-to-glowstone mutation and 10,625/10,625 raw block-light cells exact |
| redstone lamp power-on seed 0 | 1 tick | 17 matches, zero divergences, exact WEST/EAST notification and weak-power query | redstone block addition plus lamp 123-to-124, 10,625/10,625 block/light cells exact |
| redstone lamp delayed-off seed 0 | 5 ticks | 17 matches, zero divergences, exact +4 pending lifetime and dispatch | source removal plus lamp 124-to-123, 10,625/10,625 block/light cells exact |
| saved powered-lamp callback seed 0 | 4 ticks | 17 matches, zero divergences, capsule restores due/priority/order and tick-2 drain | powered callback is an exact no-op, 10,625/10,625 blocks exact |
| lever lamp power-on seed 0 | 1 tick | 17 matches, zero divergences, floor lever metadata 5-to-13 provides weak power | lever metadata plus lamp 123-to-124, 10,625/10,625 block/light cells exact |
| lever lamp delayed-off seed 0 | 5 ticks | 17 matches, zero divergences, metadata 13-to-5 creates exact +4 lamp update | lever metadata plus lamp 124-to-123, 10,625/10,625 block/light cells exact |
| saved powered-lever lamp callback seed 0 | 4 ticks | 17 matches, zero divergences, capsule recognizes powered lever and restores callback | powered callback drains as exact no-op, 10,625/10,625 blocks exact |
| lever strong power through stone seed 0 | 1 tick | 18 matches, zero divergences, on-add lamp resolves the support's directional strong input | sole air-to-124 lamp mutation and 10,625/10,625 block/light cells exact |
| lever strong-power loss seed 0 | 5 ticks | 18 matches, zero divergences, removal notifies around support and creates the exact +4 lamp callback | lever removal plus lamp 124-to-123, 10,625/10,625 block/light cells exact |
| lever strong power through planks seed 0 | 1 tick | 18 matches, zero divergences, registry-backed normal-cube support carries the same directional input | sole air-to-124 lamp mutation and 10,625/10,625 block/light cells exact |
| lever strong-power loss through planks seed 0 | 5 ticks | 18 matches, zero divergences, registry-backed callback proof retains and dispatches the exact +4 lamp update | lever removal plus lamp 124-to-123, 10,625/10,625 block/light cells exact |
| saved lever/planks powered-lamp callback seed 0 | 4 ticks | 18 matches, zero divergences, capsule restores due/priority/order through a plank support and drains powered no-op | zero mutations, 10,625/10,625 blocks exact |
| redstone block through planks negative seed 0 | 1 tick | 18 matches, zero divergences, oracle proves weak redstone-block output does not become indirect strong power | sole air-to-123 unlit-lamp placement, 10,625/10,625 block/light cells exact |
| six lever strong-power orientations seed 0 | 6 staggered edits | 18 matches, zero divergences, DOWN/UP/NORTH/SOUTH/WEST/EAST all light in their edit tick | six air-to-124 mutations and 10,625/10,625 block/light cells exact |
| saved stone-button pulse seed 0 | 25 ticks | 17 matches, zero divergences, button pending 0-18, release 19, lamp pending 19-22, off 23 | button 77:13-to-77:5 plus lamp 124-to-123, 10,625/10,625 block/light cells exact |
| saved wall-button pulse seed 0 | 7 ticks | 18 matches, zero divergences, metadata 9 retains orientation as 1 at +3 and hands off to the lamp at +4 | button 77:9-to-77:1 plus lamp 124-to-123, 10,625/10,625 block/light cells exact |
| saved ceiling/down-face button pulse seed 0 | 7 ticks | 18 matches, zero divergences, metadata 8 retains orientation as 0 at +3 and notifies its support above | button 77:8-to-77:0 plus lamp 124-to-123, 10,625/10,625 block/light cells exact |
| saved west-face button pulse seed 0 | 7 ticks | 18 matches, zero divergences, metadata 10 retains orientation as 2 and hands off to the outward lamp | button 77:10-to-77:2 plus lamp 124-to-123, 10,625/10,625 block/light cells exact |
| saved south-face button pulse seed 0 | 7 ticks | 18 matches, zero divergences, metadata 11 retains orientation as 3 and hands off to the outward lamp | button 77:11-to-77:3 plus lamp 124-to-123, 10,625/10,625 block/light cells exact |
| saved north-face button pulse seed 0 | 7 ticks | 18 matches, zero divergences, metadata 12 retains orientation as 4 and hands off to the outward lamp | button 77:12-to-77:4 plus lamp 124-to-123, 10,625/10,625 block/light cells exact |
| wooden-button arrow occupancy seed 0 | 32 ticks | 18 matches, zero divergences, exact stationary arrow state and +30 callback replacement | button 143:5-to-143:13 plus lamp 123-to-124, 10,625/10,625 block/light cells exact |
| saved wooden-button release seed 0 | 7 ticks | 18 matches, zero divergences, arrow-free callback releases at +3 and hands the lamp its independent +4 callback | button 143:13-to-143:5 plus lamp 124-to-123, 10,625/10,625 block/light cells exact |
| repeater delay 1/2/3/4 power-on seed 0 | 4/6/8/10 ticks | 18 matches, zero divergences, block 93 queues at +2/+4/+6/+8 with priority -1 and dispatches on the exact observation | source, repeater 93-to-94 with metadata 1/5/9/13, and lamp 123-to-124 exact |
| repeater delay-1 power-off seed 0 | 7 ticks | 18 matches, zero divergences, block 94 queues at +2 with priority -2 then hands the lamp its +4 callback | source removal, repeater 94:1-to-93:1, and lamp 124-to-123 exact |
| repeater SOUTH/NORTH/EAST power-on seed 0 | 4 ticks each | 18 matches, zero divergences, metadata 0/2/3 reads only its facing input and powers only its opposite output | three exact source/repeater/lamp mutation triples and raw light exact |
| repeater one-observation minimum pulse seed 0 | 8 ticks | 18 matches, zero divergences, due block 93 powers despite lost input, queues block 94 at +2/priority -1, then lamp +4 | exact return to the initial source/repeater/lamp blocks and light |
| repeater strong power through stone seed 0 | 4 ticks | 18 matches, zero divergences, powered repeater strongly powers its output normal cube directionally | source and repeater activate; lamp beyond unchanged stone turns 123-to-124 |
| locked repeater power-on seed 0 | 4 ticks | 18 matches, zero divergences, perpendicular powered repeater suppresses the main callback for every observation | source-only mutation; main stays 93 and lamp stays 123 |
| repeater unlock power-on seed 0 | 5 ticks | 18 matches, zero divergences, side-repeater removal notifies the main block and starts its full +2 delay | lock removal, source addition, main 93-to-94, and lamp 123-to-124 exact |
| repeater chain priority seed 0 | 6 ticks | 18 matches, zero divergences, upstream topology selects priority -3 before downstream priority -1 | source, two repeater transitions, and endpoint lamp mutation exact |
| saved repeater power-on seed 0 | 5 ticks | 18 matches, zero divergences, capsule restores block-93 absolute due/priority/order and dispatches it | repeater 93:1-to-94:1 and lamp 123-to-124 exact |
| saved repeater power-off seed 0 | 8 ticks | 18 matches, zero divergences, capsule restores block 94 at priority -2 then exact lamp handoff | repeater 94:1-to-93:1 and lamp 124-to-123 exact |
| saved repeater minimum pulse seed 0 | 9 ticks | 18 matches, zero divergences, restored block 93 forces block 94 at +2 and drains the lamp at +4 | exact return to the initial repeater/lamp state and light |
| comparator four-direction power-on seed 0 | 4 ticks each | 19 matches, zero divergences, all horizontal facings read only their rear input and queue +2/priority 0 | source, comparator 149-to-150, tile output 15, and lamp 123-to-124 exact |
| comparator analog rear-strength 7 seed 0 | 4 ticks | 19 matches, zero divergences, tile output and directional weak/strong power remain exactly 7 | source/dust/comparator/lamp transition and raw light exact |
| comparator compare-mode side strengths 5/7/8 seed 0 | 4 ticks each | 19 matches, zero divergences, side below/equal rear permits power while side above rear stores output 7 but leaves block 149 unpowered | exact source/dust/comparator/lamp transitions for all three thresholds |
| comparator subtract-mode side strengths 5/7/8 seed 0 | 4 ticks each | 19 matches, zero divergences, output is max(rear-side,0), including Java's unchanged-zero callback/no-callback boundary | exact output 2/0/0 tile state, queue, blocks, and light |
| saved comparator power-on seed 0 | 5 ticks | 19 matches, zero divergences, capsule restores output 0 plus block-149 due/priority/order then dispatches | comparator 149:1-to-150:1, tile output 15, and lamp 123-to-124 exact |
| saved comparator power-off seed 0 | 8 ticks | 19 matches, zero divergences, capsule restores output 15 plus block-150 callback then drains the lamp | comparator 150:9-to-149:9, tile output 0, and lamp 124-to-123 exact |
| comparator-to-repeater priority seed 0 | 6 ticks | 19 matches, zero divergences, diode output topology queues comparator at priority -1 before the downstream repeater -1 | exact comparator/repeater/lamp queue and mutation chain |
| cauldron level-3 comparator override seed 0 | 4 ticks | 19 matches, zero divergences, direct level property queues +2 and becomes tile output 3 | cauldron metadata, comparator, lamp, and corrected transparent light path exact |
| three-bite cake comparator override seed 0 | 4 ticks | 19 matches, zero divergences, output follows `(7-bites)*2=8` | cake addition, comparator/lamp, and transparent light path exact |
| End-frame eye comparator override seed 0 | 4 ticks | 19 matches, zero divergences, eye false-to-true becomes output 15 | frame metadata, comparator, lamp, and raw light exact |
| cauldron through one stone comparator seed 0 | 4 ticks | 19 matches, zero divergences, override edit notifies and is read through exactly one normal cube | cauldron, unchanged stone, comparator/lamp, and raw light exact |
| saved cauldron-through-stone comparator seed 0 | 5 ticks | 19 matches, zero divergences, capsule restores the two-block input proof, tile output 0, and callback | comparator commits tile output 3 and powers its lamp exactly |
| saved single-chest comparator seed 0 | 4 ticks | 20 matches, zero divergences, capsule restores one full stone stack in slot 0 plus comparator output 0 and its pending callback | callback commits output 1, powers the lamp, preserves all 27 slots, and matches raw block/light state |
| saved double-chest comparator seed 0 | 4 ticks | 20 matches, zero divergences, capsule restores reciprocal 27-slot halves, four full stacks, output 0, and the pending callback | combined 54-slot fullness commits output 2 (not single-half output 3), powers the lamp, and matches raw block/light state |
| saved furnace comparator seed 0 | 4 ticks | 20 matches, zero divergences, capsule restores all three slots and four furnace progress fields plus comparator output 0 and its pending callback | one full input stack commits output 5, powers the lamp, preserves exact tile state, and matches raw block/light state |
| saved closed trapped-chest comparator seed 0 | 4 ticks | 20 matches, zero divergences, capsule restores one full stone stack, closed viewer/lid state, output 0, and the pending callback | callback commits output 1, powers the lamp, preserves all 27 slots, and matches raw block/light state |
| saved closed double trapped-chest comparator seed 0 | 4 ticks | 20 matches, zero divergences, capsule restores reciprocal ID-146 halves, four full stacks, output 0, and the pending callback | combined 54-slot fullness commits output 2 (not single-half output 3), powers the lamp, and matches raw block/light state |
| live trapped-chest viewer power seed 0 | use at tick 2, close at tick 7, 16 observations | 20 matches, zero divergences, exact viewer `0→1→0`, lid `0→0.4→0`, successful-use swing cooldown, and two ordered +4 callbacks | direct weak and upward-strong lamps power transiently and both return exactly to the shared baseline |
| saved dispenser comparator seed 0 | 4 ticks | 20 matches, zero divergences, capsule restores an exact nine-slot ID-23 tile with one full stone stack, output 0, and the pending callback | callback commits output 2, powers the lamp, preserves all nine slots, and matches raw block/light state |
| saved dropper comparator seed 0 | 4 ticks | 20 matches, zero divergences, capsule restores the same bounded nine-slot state under distinct block ID 158 | inherited vanilla fullness commits output 2 with exact tile, queue, block, and light state |
| saved jukebox record-13 comparator seed 0 | 4 ticks | 20 matches, zero divergences, capsule restores exact ID-84 `has_record` metadata, record item 2256, output 0, and pending callback | callback commits output 1 and powers the lamp with exact tile/block/light state |
| saved jukebox record-wait comparator seed 0 | 4 ticks | 20 matches, zero divergences, the independent upper-bound fixture restores record item 2267 | callback commits output 12, proving the complete 1..12 record-index range |
| saved impulse/repeating/chain command-block comparator seed 0 | 4 ticks each | 20 matches, zero divergences, capsule restores only exact inert default state plus `successCount=7`, output 0, and pending comparator callback | all three IDs commit output 7 and power the lamp with exact tile/queue/block/light state; no command execution is claimed |
| saved item-frame rotation-6 comparator seed 0 | 4 ticks | 21 matches, zero divergences, capsule restores one exact stone-bearing frame behind one represented normal cube plus comparator output 0 and its pending callback | callback commits output 7, powers the lamp, preserves exact frame/queue state, and produces the two exact comparator/lamp mutations |
| observer six-face pulse seed 0 | 6 fixtures, 8 setup-drain ticks plus 7 observations | 21 matches, zero divergences, each facing watches only its front cell and retains exact +2 activation/+2 release callbacks | all six observer raw states and their directionally powered lamps reproduce the two-game-tick pulse |
| observer pending/powered suppression seed 0 | 8 setup-drain ticks plus 8 scripted observations | 21 matches, zero divergences, repeated watched edits do not duplicate pending work and edits during the powered phase do not extend it | observer queue, powered bit, lamp, and final drain exact |
| observer non-watched-neighbor negative seed 0 | 8 setup-drain ticks plus 5 observations | 21 matches, zero divergences, an edit beside rather than in front of the observer schedules nothing | edit is exact while observer and lamp remain unchanged |
| observer placement pulse seed 0 | early fixture plus 8 setup-drain ticks and 7 observations | 21 matches, zero divergences, live placement starts the vanilla +2/+2 pulse lifecycle | placed observer and directional lamp transitions exact |
| observer chain ordering seed 0 | 12 setup-drain ticks plus 10 observations | 21 matches, zero divergences, same-time callbacks and downstream notifications retain Java order | both observers and endpoint lamp reproduce the exact chained pulses |
| saved pending observer activation seed 0 | 7 observations | 21 matches, zero divergences, capsule restores block 218 due/priority/order and dispatches its activation/release lifecycle | observer and lamp transitions exact after reload |
| powered pending observer break seed 0 | 8 setup-drain ticks plus scripted break | 21 matches, zero divergences, removal during the pending powered phase notifies the output neighborhood through a normal cube | observer removal and indirect lamp's +4 delayed-off callback exact |
| normal piston empty extension start seed 0 | 1 observation | 21 matches, zero divergences, side redstone-block power creates no scheduled-tick entry because the block event drains in the same server tick | source, base 33:5-to-33:13, and moving head 36:5 are exact |
| normal piston DOWN/UP/NORTH/SOUTH/WEST empty extension starts seed 0 | 1 observation each | 21 matches, zero divergences, each metadata-facing pair resolves the same immediate block-event boundary | all five source/base/moving-head transitions are exact |
| normal piston powered-lever empty extension start seed 0 | 1 observation | 21 matches, zero divergences, floor lever 69:5-to-69:13 directly powers the side of an EAST piston in the same tick | lever, base 33:5-to-33:13, and moving head 36:5 are exact |
| normal piston powered stone/wooden-button empty extension starts seed 0 | 1 observation each | 21 matches, zero divergences, button 77/143 metadata 5-to-13 supplies direct weak side power without claiming an interaction-created release callback | each button, base 33:5-to-33:13, and moving head 36:5 transition is exact |
| normal piston powered stone/wood/light-heavy-weighted pressure-plate starts seed 0 | 1 observation each | 21 matches, zero divergences, plate metadata 0-to-1/1/7/1 supplies direct weak side power while isolating entity callback timing | each plate plus base 33:5-to-33:13 and moving head 36:5 is exact |
| normal piston directional floor-torch start and attached-face negative seed 0 | 1 observation each | 21 matches, zero divergences, lit torch weak output powers the queried side except its metadata-derived attachment face | side-torch/base/moving-head transition exact; top-torch negative keeps the base retracted and all 10,625 block-light cells exact |
| normal piston directional powered-repeater start and rotated negative seed 0 | 1 observation each | 21 matches, zero divergences, powered repeater output drives only the metadata-derived face | oriented repeater/base/moving-head transition exact; rotated repeater is a source-only mutation with the piston retracted |
| normal piston saved powered-comparator start and rotated negative seed 0 | 1 observation each | 21 matches, zero divergences, powered comparator requires positive saved tile output and the metadata-derived output face | oriented comparator extends the tick-zero piston; rotated comparator leaves it retracted; tile output 15 and emitted block light 9 are exact |
| normal piston live observer-pulse start and rotated negative seed 0 | 3 observations each after 8 setup-drain ticks | 21 matches, zero divergences, a watched-face edit creates the exact +2 observer callback and powered metadata while output is restricted to the metadata-derived face | SOUTH-watching observer 218:3-to-218:11 extends the north-side piston; EAST-watching 218:5-to-218:13 leaves it retracted; edits and light are exact |
| normal piston saved powered-dust start and perpendicular-line negative seed 0 | 1 observation each | 21 matches, zero divergences, dust weak power follows its represented horizontal connection axis | south-connected dust extends the north-side piston; east-west dust beside the same piston leaves it retracted |
| normal piston indirect dust/cube start and non-facing-dust negative seed 0 | 1 observation each | 21 matches, zero divergences, a Java-normal cube relays the strongest represented neighbor output | dust on top strongly powers the adjacent stone and extends the piston; east-west dust beyond that stone does not strongly power or relay through it |
| normal piston redstone-block quasi-connectivity plus front/below negatives seed 0 | 1 observation each | 21 matches, zero divergences, the fixed `pos.up()` neighborhood accepts the one-up/one-side source while the direct loop excludes the piston output face | quasi source extends; front/output-face and mirrored below-diagonal blocks leave the piston retracted |
| normal piston empty extension progress seed 0 | 2 observations | 21 matches, zero divergences, moving tile advances 0-to-0.5-to-1 in the active set while raw block 36:5 remains | source, extended base, and moving head remain exact |
| normal piston empty extension settle seed 0 | 3 observations | 21 matches, zero divergences, the third tile tick retires the active movement | moving block 36:5 settles to head 34:5 exactly |
| normal piston single-stone push start/progress/settle seed 0 | 1/2/3 observations | 21 matches, zero divergences, two fixed-pool moving tiles share the 0-to-0.5-to-1 lifecycle | start/progress have moving head and stone as 36:5; tick three settles front to head 34:5 and destination to stone 1:0 |
| normal piston straight stone-line traversal and 12-block limit seed 0 | two-stone start/settle, 12-stone start, 13-stone rejection | 21 matches and zero divergences in every row; the bounded far-to-near traversal allocates one moving tile per stone plus the head | two stones settle exactly, all 12 stones enter 36:5 at the legal maximum, and a 13-stone line remains entirely retracted and intact |
| normal piston dandelion DESTROY reactions seed 0 | front flower and stone-then-terminal-flower starts | 21 matches, zero divergences, the exact dropped-item state and piston/item tick order agree with Java | front flower is destroyed before extension; a terminal flower is destroyed after traversing the pushable stone |
| normal piston allium DESTROY reaction seed 0 | front red flower 38:2 start | 21 matches, zero divergences, BlockFlower metadata 2 is preserved in the exact EntityItem state and swept trajectory | allium becomes moving head 36:5 plus one dropped item 38:2 |
| normal piston floor-torch DESTROY reaction seed 0 | front supported torch 50:5 start | 21 matches, zero divergences, explicit drop mapping removes orientation from item damage while preserving all other entity fields | floor torch becomes moving head 36:5 plus one dropped item 50:0 |
| normal piston redstone-wire DESTROY reaction seed 0 | front supported wire 55:0 start | 21 matches, zero divergences, explicit drop mapping changes the block ID to the registered redstone item ID | wire becomes moving head 36:5 plus one dropped item 331:0 |
| normal piston zero-drop fire DESTROY reaction seed 0 | source-added stone-then-terminal-fire start | 21 matches, zero divergences, the pending block-51 callback is retained while no local entity, drop RNG, or item-ID consumption occurs | source powers the staged piston; stone and fire cells become moving blocks 36:5 with no item drop |
| normal piston suppressed snow-layer drops seed 0 | front supported snow 78:3 start | 21 matches, zero divergences, Forge's five candidate snowball stacks consume five chance draws but chance -1 suppresses every entity | snow becomes moving head 36:5, with no snowball entity, Math.random draw, or entity-ID advance |
| normal piston brown/red mushroom DESTROY reactions seed 0 | front supported mushroom 39:0 and 40:0 starts | 21 matches and zero divergences in both rows; the shared BlockMushroom default-drop path retains each registered block/item ID | each mushroom becomes moving head 36:5 plus one exact item 39:0 or 40:0 |
| normal piston attached-ladder DESTROY reaction seed 0 | staged east-facing piston supports ladder 65:5; tick zero adds a side redstone block | 21 matches, zero divergences, the indirect piston block event drains at the restored input boundary and orientation metadata strips to item damage zero | source/base/ladder become 152:0/33:13/36:5 plus one exact item 65:0 |
| normal piston cobweb DESTROY reaction seed 0 | front support-independent cobweb 30:0 start | 21 matches, zero divergences, `BlockWeb.getItemDropped` maps the block to registered string item 287:0 | cobweb becomes moving head 36:5 plus one exact string item |
| normal piston ordinary/lit pumpkin DESTROY reactions seed 0 | front pumpkin 86:3 and lit pumpkin 91:3 starts | 21 matches and zero divergences in both rows; shared `BlockPumpkin` facing metadata strips to damage zero | each pumpkin becomes moving head 36:5 plus exact item 86:0/91:0; lit-pumpkin block light drains exactly |
| normal piston structure-void DESTROY reaction seed 0 | front structure void 217:0 start | 21 matches, zero divergences, its empty drop override consumes no drop RNG, Math.random, entity ID, or capacity; successful extension still consumes its later pitch draw | structure void becomes moving head 36:5 with no item entity |
| normal piston cake DESTROY and comparator teardown seed 0 | whole-cake start, three-bite cake/comparator/lamp circuit, and saved settled-head comparator | exact zero-drop/entity/RNG state; cake analog 8 clears at +2 and its lamp at +4; settled moving/head states remain valid zero-strength comparator inputs | cake becomes moving head 36:5 with no item; comparator and lamp transitions are exact through settlement |
| normal piston melon randomized DESTROY seeds 0/1 | front melon 103:0 with controlled internal World.rand cursors 0 and 1 | 24 matches, zero divergences; exact `nextInt(5)`, fortune-zero `nextInt(1)`, 3/7 separate item stacks, consecutive EIDs, chance/offset RNG, Math.random, and pitch transitions | melon becomes moving head 36:5 plus three or seven exact item 360:0 entities |
| normal piston pumpkin/melon stem randomized DESTROY seed 0 | age-0 pumpkin stem with Block.RANDOM seed 1; age-7 pumpkin/melon stems with seed 15 | 24 matches, zero divergences; every path consumes three `nextInt(15)` trials, with exact zero/three seed-stack, World/Math RNG, EID, and farmland notification outcomes | stems become moving head 36:5, supporting farmland becomes dirt, and successful trials emit item 361:0 or 362:0 |
| normal piston vine/waterlily DESTROY seed 0 | south-attached vine 106:1 and waterlily 111:0 over still water | 24 matches, zero divergences; ordinary piston break emits no vine item and one exact waterlily 111:0 item with exact RNG and EID transitions | each target becomes moving head 36:5, the vine support and source water remain unchanged, and no scheduled work is introduced |
| normal piston nether-wart randomized DESTROY seeds 0/1 | age-0 wart with World.rand cursor 0 and age-3 wart with controlled cursor 1 | 24 matches, zero divergences; immature wart emits one item 372:0 while mature `2 + nextInt(3)` emits four separate stacks, with exact World/Math RNG, EID, and pitch transitions | wart becomes moving head 36:5 over unchanged soul sand plus one or four exact nether-wart items |
| normal piston dragon-egg DESTROY seed 0 | front dragon egg 122 with normalized raw metadata | 24 matches, zero divergences; one item 122:0 is emitted with exact World/Math RNG, EID, and pitch transitions | dragon egg becomes moving head 36:5 plus one exact item |
| normal piston cocoa-age DESTROY seed 0 | supported age-0 and age-2 cocoa 127 | 24 matches, zero divergences; immature cocoa emits one dye 351:3 and mature cocoa emits three separate dyes with exact RNG, EID, and pitch transitions | cocoa becomes moving head 36:5 while its jungle-log support remains unchanged |
| normal piston isolated tripwire hook/wire DESTROY seed 0 | front hook 131 or wire 132 | 24 matches, zero divergences; exact normalized drops, item trajectories, RNG, EID, and pitch transitions | each target becomes moving head 36:5 plus item 131:0 or 287:0 |
| normal piston attached tripwire hook DESTROY seed 0 | attached three-wire line with two hooks | 24 matches, zero divergences; breaking one hook detaches the opposite hook and wires in Java notification order | moving head, detached line metadata, and exact hook item agree |
| normal piston attached tripwire wire immediate/+10 seed 0 | attached line with one wire pushed, observed immediately and after the hook callback | 24 matches, zero divergences; immediate line detachment and the saved +10 hook recheck queue agree | moving head and every surviving hook/wire metadata transition agree |
| dropped-item occupied tripwire seed 0 | attached line, stationary item over middle wire, 12 observations | 24 matches, zero divergences; item collision powers the middle wire and both hooks, preserves exact hook/wire callback ordering, and reschedules the occupied wire at +10 | lamps, hooks, wires, pending callbacks, raw blocks, and block light agree exactly |
| player crossing and leaving tripwire seed 0 | unattached line, player walks across and fully clears the middle wire, 22 observations | 24 matches, zero divergences; player collision attaches and powers the complete line, then the +10 wire callback releases power while preserving attachment | player trajectory, lamps, hooks, wires, pending callbacks, raw blocks, and block light agree exactly |
| normal piston carrot/potato crop DESTROY seed 0 | mature carrot, immature potato, and mature potato with controlled World.rand/Block.RANDOM | 24 matches, zero divergences; mature crops consume three exact `nextInt(14)` trials and mature potato appends the independently selected poisonous-potato stack | carrot/potato/poison item stacks, RNG/EID cursors, moving head, and farmland-to-dirt notification agree exactly |
| normal piston comparator DESTROY seed 0 | unpowered 149, powered 149 with output lamp, and transient powered block 150 | 24 matches, zero divergences; item 404, comparator tile retirement, output-neighbor teardown, lamp +4 release, and stale +2 comparator callback agree exactly | both comparator IDs become moving head 36:5; item, tile, scheduled queue, lamp, and RNG/EID cursors are exact |
| normal piston beetroot DESTROY seed 0 | immature age 1 and mature age 3 with controlled World.rand | 24 matches, zero divergences; immature emits one seed, mature consumes three exact `nextInt(6)` trials and emits beetroot then two seeds | item order, RNG/EID cursors, moving head, and farmland-to-dirt notification agree exactly |
| normal piston empty retraction seed 0 | settled EAST base/head, tick-zero source removal, 1/2/3 observations | 24 matches, zero divergences; contraction pitch advances World.rand 0-to-11 with unchanged EID and no scheduled work | source removal, base moving state at ticks 1/2, head removal, and unextended settlement at tick 3 are exact |
| sticky piston single-stone extension/pull seed 0 | EAST base with one stone, extension and source-removal retraction at 1/2/3 observations | 24 matches, zero divergences; extension and pull retain exact typed moving tiles, progress, RNG, EID, and empty scheduled queue | sticky head metadata 13, moved-stone metadata 5, pull origin, and final base/stone placement agree exactly |
| sticky piston empty/immovable/headless retraction seed 0 | all six empty facings; EAST obsidian, missing sticky/normal heads, and unrelated front stone plus obsidian | 24 matches, zero divergences; every start/settled path consumes one contraction draw, leaves EID unchanged, and keeps the queue empty | bases retract exactly; immovable and unrelated blocks remain untouched even when the serialized head is absent |
| normal/sticky piston one-observation minimum pulse seed 0 | EAST base with one stone; source added at tick 0 and removed at tick 1, five observations | 24 matches, zero divergences; both extension and contraction sound draws, unchanged EID, empty queue, and observer-powered reversal agree | extending head is force-settled then erased; destination stone continues, sticky pull is suppressed, and base settles retracted exactly |
| normal piston redstone-control DESTROY reactions seed 0 | front lever, stone/wood buttons, four pressure plates, lit/unlit redstone torches, and powered/unpowered repeaters | 21 matches and zero divergences in all 11 rows; exact block-specific item IDs, damage zero, RNG/entity cursors, and swept item trajectories | controls become moving head 36:5 plus item 69/77/143/70/72/147/148/76/76/356/356 respectively |
| powered-control break notifications and pre-sweep item SELF collision seed 0 | powered floor lever, stone plate, lit floor torch, and powered repeater with indirect lamps over five observations | 21 matches and zero divergences in all four rows; exact break-neighbor callbacks, lamp +4 queues, and Java-order item SELF collision against moving and settled piston-head shapes | each lamp follows exact `[124],[124],[124],[],[]` queue lifetime and 124-to-123 transition; item pose/motion remains exact |
| downward piston onto repeater surface seed 0 | unpowered repeater over stone, stationary NoAI pig, two observations | 25 matches, zero divergences; pig Y follows `79.59000002384185 -> 79.125` from the inherited full-footprint 1/8 diode box | moving piston, repeater, all 10,625 raw block/light cells, queue, and RNG state are exact |
| downward piston onto comparator surface seed 0 | unpowered comparator/output-0 tile over stone, stationary NoAI pig, two observations | 25 matches, zero divergences; the same exact 1/8 collision top is reached while comparator tile state remains exact | moving piston, comparator block/tile, all 10,625 raw block/light cells, queue, and RNG state are exact |
| downward piston onto brewing-stand center stem seed 0 | empty stand inserted at tick 0, centered stationary NoAI pig, piston inserted at tick 1, three observations | exact pig Y `80 -> 79.875 -> 79.875`; diagnostic only for the deliberately unrepresented brewing inventory tile | moving piston, stand, all 10,625 raw block/light cells, queue, and RNG state are exact |
| downward piston onto brewing-stand side base seed 0 | same stand/piston sequence with the pig west of the centered stem | exact pig Y `80 -> 79.59000002384185 -> 79.125`; diagnostic only for the deliberately unrepresented brewing inventory tile | moving piston, stand, all 10,625 raw block/light cells, queue, and RNG state are exact |
| normal piston dead-bush randomized-count DESTROY seed 0 | front sand-supported dead bush 32:0 start | 21 matches, zero divergences, exact `nextInt(3)=2`, two per-stack RNG sequences, consecutive EIDs, and independent swept trajectories | dead bush becomes moving head 36:5 plus two separate exact stick 280:0 entities |
| normal piston tall-grass randomized DESTROY seed 0 | front dirt-supported tall grass 31:1 with controlled internal `Block.RANDOM` seeds 0 and 1396 | 21 matches, zero divergences; success consumes `nextInt(8)`, `nextInt(10)`, and `nextInt(1)` before one exact wheat-seed entity, while rejection consumes only `nextInt(8)` | both branches replace tall grass with moving head 36:5; success adds item 295:0 and rejection remains entity-free |
| normal piston wheat-age DESTROY seed 0 | front farmland-supported wheat 59:7 and 59:3 with controlled internal `World.rand` seed 0 | 22 matches, zero divergences; mature wheat consumes three `nextInt(14)` trials then emits wheat 296:0 plus two separate seed 295:0 stacks, while immature wheat emits one seed with no count trial; both have exact chance/offset, Math.random, EID, and pitch transitions | both ages become moving head 36:5, and its solid material notifies farmland 60:0 to become dirt 3:0 in the same boundary |
| normal piston sapling type/stage DESTROY reactions seed 0 | front oak stage-0 sapling 6:0 and dark-oak stage-1 sapling 6:13 starts | 21 matches and zero divergences in both rows; wood type is retained while growth stage is stripped from item damage | both saplings become moving head 36:5 plus exact item 6:0 or 6:5 |
| opposed normal pistons share one extension boundary seed 0 | one center redstone placement notifies WEST then EAST | 21 matches, zero divergences; the first successful extension consumes its post-move pitch `nextFloat`, so the second sapling drop starts from the exact advanced World.rand cursor | both bases extend, both saplings become moving heads, and both exact item entities match |
| normal piston birch-planks NORMAL reaction seed 0 | start and settlement | 21 matches, zero divergences, registry-backed normal-cube movement preserves the non-stone block state | moving block 36:5 settles to birch planks 5:2 exactly |
| normal piston obsidian-blocked seed 0 | 1 observation | 21 matches, zero divergences, powered structure check rejects immovable obsidian | source-only mutation; base remains 33:5 and obsidian remains |
| saved stone-pressure-plate release seed 0 | 7 ticks | 18 matches, zero divergences, powered plate pending 0-1, release at +3, lamp pending through +6 and off at +7 | plate 70:1-to-70:0 plus lamp 124-to-123, 10,625/10,625 block/light cells exact |
| live stone-pressure-plate walkover seed 0 | 8 forward ticks | 18 matches, zero divergences, crossing activates at observation 3 and creates the exact +20 callback | plate 70:0-to-70:1 plus lamp 123-to-124, 10,625/10,625 block/light cells exact |
| occupied stone-pressure-plate reschedule seed 0 | 22 idle ticks | 18 matches, zero divergences, first callback retained 0-19 and replaced at observation 20 with the next +20 callback | plate and lamp activate once and remain powered, 10,625/10,625 block/light cells exact |
| living-mob stone-pressure-plate reschedule seed 0 | 22 idle ticks | 18 matches, zero divergences, a stationary collision-enabled pig activates at observation 0 and replaces the due callback at observation 20 | plate and lamp activate once and remain powered, pig state and 10,625/10,625 block-light cells exact |
| dropped-item wooden-pressure-plate reschedule seed 0 | 22 idle ticks | 18 matches, zero divergences, a stationary EntityItem activates at observation 0 and replaces the due callback at observation 20 | wooden plate and lamp activate once and remain powered, exact item stack/state and 10,625/10,625 block-light cells |
| two-entity gold weighted-pressure-plate seed 0 | 12 idle ticks | 18 matches, zero divergences, player plus one EntityItem map to strength 2 and replace the due callback at +10 | plate 147 and dust both settle at metadata 2, lamp powers, exact queue/entity/light state |
| two-entity iron weighted-pressure-plate seed 0 | 12 idle ticks | 18 matches, zero divergences, the same two entities map to `ceil(2/150*15)=1` and replace the due callback at +10 | plate 148 and dust both settle at metadata 1, lamp powers, exact queue/entity/light state |
| saved gold weighted-pressure-plate release seed 0 | 7 ticks | 18 matches, zero divergences, powered plate callback releases at +3 and hands the lamp its independent +4 callback | plate 147:2-to-147:0, dust 55:2-to-55:0, lamp 124-to-123, 10,625/10,625 block-light cells exact |
| one-wire lamp power-on seed 0 | 1 tick | 17 matches, zero divergences, edit-driven flat component converges in the source tick | source addition, dust 0-to-15, and lamp 123-to-124, 10,625/10,625 exact |
| one-wire lamp delayed-off seed 0 | 5 ticks | 17 matches, zero divergences, dust drains immediately and lamp callback dispatches at +4 | source removal, dust 15-to-0, and lamp 124-to-123, 10,625/10,625 exact |
| 15-wire attenuation seed 0 | 1 tick | 17 matches, zero divergences, metadata settles 15 through 1 and powers endpoint | 17 exact source/dust/lamp mutations, 10,625/10,625 exact |
| 16-wire cutoff seed 0 | 1 tick | 17 matches, zero divergences, metadata settles 15 through 1 then 0 | 16 exact source/dust mutations; endpoint lamp remains off, 10,625/10,625 exact |
| flat wire T-branch seed 0 | 1 tick | 17 matches, zero divergences, center 15 and all three leaves 14 | 5 exact source/dust mutations, 10,625/10,625 exact |
| powered wire loop removal seed 0 | 1 tick | 17 matches, zero divergences, closed component drains without stale self-power | source plus eight dust cells drain exactly, 10,625/10,625 exact |
| wire one-block climb seed 0 | 1 tick | 18 matches, zero divergences, clear-headed step attenuates 15/14/13 | source, three dust cells, and endpoint lamp mutate exactly, 10,625/10,625 light cells |
| wire one-block climb on planks seed 0 | 1 tick | 18 matches, zero divergences, captured normal-cube topology gives the same 15/14/13 climb | source, three dust cells, and endpoint lamp mutate exactly, 10,625/10,625 light cells |
| powered wire climb removal seed 0 | 5 ticks | 18 matches, zero divergences, component drains immediately and lamp dispatches at +4 | inverse five mutations and complete light drain exact |
| wire one-block descent seed 0 | 1 tick | 18 matches, zero divergences, open adjacent cell connects to lower dust at 14/13 | source, three dust cells, and endpoint lamp mutate exactly, 10,625/10,625 light cells |
| wire strong power through stone seed 0 | 1 tick | 18 matches, zero divergences, powered wire above the support is queried directionally with output enabled | sole air-to-124 lamp mutation and 10,625/10,625 block/light cells exact |
| wire strong-power loss seed 0 | 5 ticks | 18 matches, zero divergences, dust recomputes with its own output disabled and creates the exact +4 lamp callback | source removal, dust 15-to-0, and lamp 124-to-123, 10,625/10,625 block/light cells exact |
| torch strong power through stone seed 0 | 1 tick | 18 matches, zero divergences, a lit torch below the support emits strong power only on its DOWN query | sole air-to-124 lamp mutation and 10,625/10,625 block/light cells exact |
| direct torch strong-power loss seed 0 | 5 ticks | 18 matches, zero divergences, torch removal notifies the support's consumer and creates the exact +4 lamp callback | torch removal plus lamp 124-to-123, 10,625/10,625 block/light cells exact |
| scheduled torch strong-power loss seed 0 | 7 ticks | 18 matches, zero divergences, +2 torch callback hands off to an independently ordered +4 lamp callback | support 1-to-152, torch 76:5-to-75:5, and lamp 124-to-123, 10,625/10,625 block/light cells exact |
| floor torch power-off seed 0 | 3 ticks | 17 matches, zero divergences, block-76 callback pending at 0 and dispatching at 1/+2 | support 1-to-152 plus torch 76:5-to-75:5, 10,625/10,625 exact |
| floor torch power-on seed 0 | 3 ticks | 17 matches, zero divergences, block-75 callback pending at 0 and dispatching at 1/+2 | support 152-to-1 plus torch 75:5-to-76:5, 10,625/10,625 exact |
| saved floor torch power-off seed 0 | 3 ticks | 17 matches, zero divergences, capsule restores block-76 due/priority/order | sole torch 76:5-to-75:5 mutation, 10,625/10,625 exact |
| saved floor torch power-on seed 0 | 3 ticks | 17 matches, zero divergences, capsule restores block-75 due/priority/order | sole torch 75:5-to-76:5 mutation, 10,625/10,625 exact |
| saved indirect-powered lit floor torch seed 0 | 3 ticks | 25 matches, zero divergences, capsule restores block-76 callback and powered-repeater support proof | sole torch 76:5-to-75:5 mutation, 10,625/10,625 block/light cells exact |
| saved stale lit floor torch seed 0 | 3 ticks | 25 matches, zero divergences, valid callback drains at +2 without a toggle | zero mutations, 10,625/10,625 block/light cells exact |
| saved indirect-powered unlit floor torch seed 0 | 3 ticks | 25 matches, zero divergences, stale block-75 callback drains at +2 without relighting | zero mutations, 10,625/10,625 block/light cells exact |
| floor torch burnout seed 0 | 36 ticks, 16 scripted edits | 17 matches, zero divergences, eighth off edge at observation 29 creates the exact +160 recovery callback | torch remains 75:5 and all 10,625 block/light cells are exact |
| floor torch burnout recovery seed 0 | 191 ticks, same edits | 17 matches, zero divergences, recovery callback remains through observation 188 and drains on 189 | torch returns to 76:5 and all 10,625 block/light cells are exact |
| wall torch power-off seed 0 | 6 ticks, four staggered edits | 18 matches, zero divergences, all metadata 1/2/3/4 support directions dispatch at +2 | four supports and four torches mutate exactly, 10,625/10,625 block/light cells |
| wall torch power-on seed 0 | 6 ticks, four staggered edits | 18 matches, zero divergences, complementary block-75 callbacks preserve every orientation | four supports and four torches mutate exactly, 10,625/10,625 block/light cells |
| saved EAST wall torch power-off seed 0 | 3 ticks | 25 matches, zero divergences, capsule restores metadata-1 block-76 callback due at +2 | sole torch 76:1-to-75:1 mutation, 10,625/10,625 block/light cells exact |
| saved EAST wall torch power-on seed 0 | 3 ticks | 25 matches, zero divergences, capsule restores metadata-1 block-75 callback due at +2 | sole torch 75:1-to-76:1 mutation, 10,625/10,625 block/light cells exact |
| saved WEST wall torch power-off seed 0 | 3 ticks | 25 matches, zero divergences, capsule restores metadata-2 block-76 callback due at +2 | sole torch 76:2-to-75:2 mutation, 10,625/10,625 block/light cells exact |
| saved WEST wall torch power-on seed 0 | 3 ticks | 25 matches, zero divergences, capsule restores metadata-2 block-75 callback due at +2 | sole torch 75:2-to-76:2 mutation, 10,625/10,625 block/light cells exact |
| saved SOUTH wall torch power-off seed 0 | 3 ticks | 25 matches, zero divergences, capsule restores metadata-3 block-76 callback due at +2 | sole torch 76:3-to-75:3 mutation, 10,625/10,625 block/light cells exact |
| saved SOUTH wall torch power-on seed 0 | 3 ticks | 25 matches, zero divergences, capsule restores metadata-3 block-75 callback due at +2 | sole torch 75:3-to-76:3 mutation, 10,625/10,625 block/light cells exact |
| saved NORTH wall torch power-off seed 0 | 3 ticks | 25 matches, zero divergences, capsule restores metadata-4 block-76 callback due at +2 | sole torch 76:4-to-75:4 mutation, 10,625/10,625 block/light cells exact |
| saved NORTH wall torch power-on seed 0 | 3 ticks | 25 matches, zero divergences, capsule restores metadata-4 block-75 callback due at +2 | sole torch 75:4-to-76:4 mutation, 10,625/10,625 block/light cells exact |
| saved top-slab floor torch power-on seed 0 | 3 ticks | 25 matches, zero divergences, capsule admits the Forge UP-solid top half of slab 44:8 | sole torch 75:5-to-76:5 mutation, 10,625/10,625 block/light cells exact |
| saved top-stair floor torch power-on seed 0 | 3 ticks | 25 matches, zero divergences, capsule admits the Forge UP-solid top half of stair 53:4 | sole torch 75:5-to-76:5 mutation, 10,625/10,625 block/light cells exact |
| saved full-snow floor torch power-on seed 0 | 3 ticks | 25 matches, zero divergences, capsule admits only the eight-layer 78:7 support | sole torch 75:5-to-76:5 mutation, 10,625/10,625 block/light cells exact |
| saved hopper floor torch power-on seed 0 | 3 ticks | 25 matches, zero divergences, capsule admits hopper UP and lighting retains Java's registered opacity zero | sole torch 75:5-to-76:5 mutation; hopper receives block light 6 and all 10,625 cells are exact |
| saved farmland wall torch power-on seed 0 | 3 ticks | 25 matches, zero divergences, capsule admits farmland's horizontal side-solid override | sole torch 75:1-to-76:1 mutation, 10,625/10,625 block/light cells exact |
| saved stair-side wall torch power-on seed 0 | 3 ticks | 25 matches, zero divergences, capsule resolves the stair's actual shape and EAST-solid face | sole torch 75:1-to-76:1 mutation, 10,625/10,625 block/light cells exact |
| saved oak-fence floor torch power-on seed 0 | 3 ticks | 25 matches, zero divergences, capsule admits the BlockFence top exception | sole torch 75:5-to-76:5 mutation; support receives light 6 and all 10,625 cells are exact |
| saved nether-fence floor torch power-on seed 0 | 3 ticks | 25 matches, zero divergences, capsule admits the rock-material BlockFence variant | sole torch 75:5-to-76:5 mutation; support receives light 6 and all 10,625 cells are exact |
| saved spruce-fence floor torch power-on seed 0 | 3 ticks | 25 matches, zero divergences, registry ID 188 retains BlockFence opacity zero | sole torch 75:5-to-76:5 mutation; support receives light 6 and all 10,625 cells are exact |
| saved birch-fence floor torch power-on seed 0 | 3 ticks | 25 matches, zero divergences, registry ID 189 retains BlockFence opacity zero | sole torch 75:5-to-76:5 mutation; support receives light 6 and all 10,625 cells are exact |
| saved jungle-fence floor torch power-on seed 0 | 3 ticks | 25 matches, zero divergences, registry ID 190 retains BlockFence opacity zero | sole torch 75:5-to-76:5 mutation; support receives light 6 and all 10,625 cells are exact |
| saved dark-oak-fence floor torch power-on seed 0 | 3 ticks | 25 matches, zero divergences, registry ID 191 retains BlockFence opacity zero | sole torch 75:5-to-76:5 mutation; support receives light 6 and all 10,625 cells are exact |
| saved acacia-fence floor torch power-on seed 0 | 3 ticks | 25 matches, zero divergences, registry ID 192 retains BlockFence opacity zero | sole torch 75:5-to-76:5 mutation; support receives light 6 and all 10,625 cells are exact |
| saved glass floor torch power-on seed 0 | 3 ticks | 25 matches, zero divergences, capsule admits the explicit glass top exception | sole torch 75:5-to-76:5 mutation; support receives light 6 and all 10,625 cells are exact |
| saved stained-glass floor torch power-on seed 0 | 3 ticks | 25 matches, zero divergences, ID 95 is promoted with hardness 0.3 and opacity zero | sole torch 75:5-to-76:5 mutation; support receives light 6 and all 10,625 cells are exact |
| saved cobblestone-wall floor torch power-on seed 0 | 3 ticks | 25 matches, zero divergences, capsule admits the explicit wall top exception | sole torch 75:5-to-76:5 mutation; support receives light 6 and all 10,625 cells are exact |
| floor torch support removal seed 0 | 1 tick | 25 matches, zero divergences, exact four-draw item spawn and empty scheduled queue | support and torch become air; item 76:0 and 10,625/10,625 block-light cells exact |
| wall torch support removal seed 0 | 1 tick | 25 matches, zero divergences, metadata-1 attachment loss emits the same lit-torch item | support and torch become air; item/RNG/entity/light state exact |
| supported torch unrelated-neighbor removal seed 0 | 1 tick | 25 matches, zero divergences, zero item/RNG/queue side effects | only the unrelated stone becomes air; torch and light remain exact |
| powered floor-lever support removal seed 0 | 5 ticks | 25 matches, zero divergences, exact item/RNG/entity cursors and powered-support lamp handoff | support and lever become air, item 69:0 advances through ages 1..5, and lamp turns off at +4 exactly |
| supported lever unrelated-neighbor removal seed 0 | 1 tick | 25 matches, zero divergences, stored floor support remains valid with zero item/RNG/queue side effects | only the unrelated alternate support becomes air; lever, lamp, and light remain exact |
| powered wall stone-button support removal seed 0 | 5 ticks | 25 matches, zero divergences, exact item 77:0 spawn and retained saved +20 stale callback | support and button become air; lamp hands off at +4 while the normalized stale button work remains queued |
| powered ceiling wooden-button support removal seed 0 | 5 ticks | 25 matches, zero divergences, exact item 143:0 spawn and ordered +3 stale-button/+4 lamp dispatch | support and button become air; stale button work drains without resurrection and lamp turns off exactly |
| powered stone-pressure-plate support removal seed 0 | 5 ticks | 25 matches, zero divergences, exact item 70:0 spawn and ordered +3 stale-plate/+4 lamp dispatch | support and plate become air; stale callback drains without resurrection and lamp turns off exactly |
| powered wooden-pressure-plate support removal seed 0 | 5 ticks | 25 matches, zero divergences, exact item 72:0/RNG/entity state and powered-neighbor teardown | support and plate become air, item advances through ages 1..5, and lamp turns off at +4 exactly |
| powered gold weighted-pressure-plate support removal seed 0 | 5 ticks | 25 matches, zero divergences, strength-2 item normalization and +3 stale callback exact | support and plate become air; item 147:0, stale queue, lamp, and light are exact |
| powered iron weighted-pressure-plate support removal seed 0 | 5 ticks | 25 matches, zero divergences, strength-1 item normalization and +3 stale callback exact | support and plate become air; item 148:0, stale queue, lamp, and light are exact |
| stone pressure plate on fence negative seed 0 | 1 tick | 25 matches, zero divergences, oak fence remains a valid explicit support with zero drop/RNG/queue side effects | only the unrelated east stone becomes air; fence and plate remain exact |
| unpowered repeater support removal seed 0 | 5 ticks | 25 matches, zero divergences, exact item 356:0 spawn and retained saved block-93 callback | support and repeater become air; stale callback drains without resurrection |
| powered repeater support removal seed 0 | 5 ticks | 25 matches, zero divergences, exact item/RNG/entity state plus ordered stale repeater and +4 lamp work | support and repeater become air; lamp turns off at +4 exactly |
| unpowered comparator support removal seed 0 | 5 ticks | 25 matches, zero divergences, exact item 404:0 and comparator tile retirement | support and comparator become air; saved callback drains stale with no output work |
| powered comparator support removal seed 0 | 5 ticks | 25 matches, zero divergences, tile retires before output notification and lamp queues at +4 | support/comparator removal, item 404:0, lamp release, queue, and light are exact |
| transient powered comparator support removal seed 0 | 5 ticks | 25 matches, zero divergences, block 150 normalizes to item 404:0 and retains its stale callback | support/transient comparator removal and lamp 124-to-123 are exact |
| repeater on top-slab negative seed 0 | 1 tick | 25 matches, zero divergences, stateful fully-opaque top slab retains the repeater with zero item/RNG/queue work | only the unrelated east stone becomes air; support and repeater remain exact |
| unpowered tripwire-hook support removal seed 0 | 1 tick | 25 matches, zero divergences, stored-facing wall loss emits exact item 131:0 and advances exact RNG/entity cursors | support and hook become air with no scheduled work; raw blocks and light are exact |
| tripwire-hook alternate-support removal seed 0 | 1 tick | 25 matches, zero divergences, removing an unrelated valid side leaves the stored north attachment intact | only the alternate stone becomes air; hook/RNG/entity/queue state remain exact |
| powered attached tripwire-hook support removal seed 0 | 5 ticks | 25 matches, zero divergences, exact line detach, two ordered +4 lamp callbacks, and retained +10 stale hook callback | support/hook removal, three detached wire states, opposite hook release, both lamps, item, and light are exact |
| attached tripwire direct string removal seed 0 | 14 ticks | 25 matches, zero divergences, queue-free start, exact +10 hook pulse, and east/west +14 lamp release | removed string, hooks, remaining wire metadata, lamps, and all 10,625 light cells are exact |
| attached tripwire direct hook removal seed 0 | 1 tick | 25 matches, zero divergences, no item/queue and exact two-draw detach-sound RNG transition | removed hook, opposite hook, and all three detached wire states are exact |
| tripwire completed-line on-add seed 0 | 10 ticks | 25 matches, zero divergences, queue-free detached start, exact five-state attach, and one +10 callback | new middle string, both hooks, all three attached wire states, and light are exact |
| isolated tripwire on-add negative seed 0 | 1 tick | 25 matches, zero divergences, no hook scan result, RNG, entity, or scheduled side effect | sole air-to-detached-string mutation is exact |
| attached tripwire shears-disarm harvest seed 0 | 1 controlled harvest | 25 matches, zero divergences, exact shears 0-to-1 damage, 0.005 exhaustion, string entity, six-draw World RNG, Math RNG/EID cursors, and one +10 hook callback | middle string becomes air; both hooks and the two remaining strings detach without an alarm pulse; 10,625/10,625 blocks and light cells exact |
| powered stone pressure plate direct removal seed 0 | 5 ticks | 25 matches, zero divergences, queue-free start and exact support-neighborhood +4 handoff | plate becomes air and support-adjacent lamp turns off exactly |
| powered wooden pressure plate direct removal seed 0 | 5 ticks | 25 matches, zero divergences, binary strength-15 break lifecycle and unchanged RNG/entity state | plate removal, lamp release, queue, and light are exact |
| powered gold weighted pressure plate direct removal seed 0 | 5 ticks | 25 matches, zero divergences, analog metadata-2 still invokes powered break lifecycle | plate removal, lamp release, queue, and light are exact |
| powered iron weighted pressure plate direct removal seed 0 | 5 ticks | 25 matches, zero divergences, analog metadata-1 support notification and +4 dispatch | plate removal, lamp release, queue, and light are exact |
| unpowered stone pressure plate direct removal negative seed 0 | 1 tick | 25 matches, zero divergences, no support callback, item, RNG, entity, or scheduled work | sole plate-to-air mutation is exact |
| direct comparator aligned/wrong-direction placement seed 0 | 7/1 ticks | 25 matches, zero divergences, output-0 tile is created after the directional callback; aligned placement queues stale repeater release at +2 and lamp release at +6, while rotated placement queues nothing | aligned comparator/repeater/lamp transitions and rotated comparator-only mutation are exact, with unchanged RNG/items and raw light |
| direct wire placement beside powered/unpowered repeater seed 0 | 3/1 ticks | 25 matches, zero divergences, zero-power dust runs the complete on-add vertical ring; powered diagonal repeater queues and releases at +2, while unpowered control remains queue-free | wire placement plus powered repeater 94:1-to-93:1 are exact; control mutates only air-to-wire, with unchanged RNG/items and light |
| direct lamp lit-unpowered/unlit-powered/unlit-unpowered placement seed 0 | 1 tick each | 25 matches, zero divergences, on-add normalization immediately selects 123/124/123 with no scheduled callback, item, or RNG work | each requested air-to-lamp transition settles to the exact powered state and all 10,625 block-light cells match |
| direct lit/unlit redstone-torch placement seed 0 | 1 tick each | 25 matches, zero divergences; lit on-add queues the diagonal powered repeater, direct unpowered repeater, and powered-support self callback in exact dispatch order, while unlit remains queue-free | sole air-to-torch mutation and all 10,625 block-light cells are exact with unchanged RNG/items |
| live block-light removal seed 0 | 1 tick | 17 matches, zero divergences, pre-existing source removed at tick 0 | one glowstone-to-air mutation and all 10,625 raw block-light cells drain exactly |
| live block-light opacity seed 0 | 1 tick | 17 matches, zero divergences, opaque stone inserted beside a live source | blocker 14-to-0 and rerouted cell 13-to-11, 10,625/10,625 exact |
| live block-light overlap seed 0 | 1 tick | 17 matches, zero divergences, second glowstone inserted into a live field | second source 11-to-15, overlap edge 12-to-14, at least 1,000 changed light cells exact |
| live block-light chunk edge seed 0 | 1 tick | 17 matches, zero divergences, glowstone placed at local x=15 | source 0-to-15 and next-chunk x=16 neighbor 0-to-14, more than 2,000 changed light cells exact |
| live skylight roof addition seed 0 | 1 tick | 17 matches, zero divergences, exact saved pre-light restored before a real tick-0 placement | source 15-to-0 plus eight shadow cells 15-to-14, 10,625/10,625 raw skylight cells exact |
| live skylight roof removal seed 0 | 1 tick | 17 matches, zero divergences, pre-existing roof removed at tick 0 | source 0-to-15 plus eight shadow cells 14-to-15, 10,625/10,625 raw skylight cells exact |
| live skylight roof chunk edge seed 0 | 1 tick | 17 matches, zero divergences, opaque roof placed at local x=15 | the same exact nine-cell transition at a chunk boundary, 10,625/10,625 raw skylight cells exact |
| falling sand landing seed 0 | 20 ticks | 17 matches, zero divergences, exact 9-tick entity trajectory and stability queue | source removal plus landing placement, 2 mutations exact |
| falling gravel landing seed 0 | 20 ticks | 26 matches, zero divergences, exact 9-tick entity trajectory and stability queue | source removal plus landing placement, 2 mutations exact |
| falling sand through still water seed 0 | 20 ticks | 26 matches, zero divergences, exact 9-tick entity trajectory and stability queue | source removal plus water-to-sand replacement, 2 mutations and block light exact |
| falling gravel through static lava seed 0 | 20 ticks | 26 matches, zero divergences, exact 9-tick entity trajectory and stability queue | source removal plus lava-to-gravel replacement, 2 mutations and block light exact |
| falling sand through fire seed 0 | 20 ticks | 26 matches, zero divergences, exact 9-tick entity trajectory and stability queue | source removal plus fire-to-sand replacement, 2 mutations and block light exact |
| falling sand failed placement on bottom slab seed 0 | 13 ticks | diagnostic ambient entity cursor; exact 11-tick trajectory and tick-12 item type/count/age, with a separate event-local sand/gravel command oracle strict over 24 updates | sole source removal, slab unchanged, and all 10,625 blocks and block-light cells exact |
| falling sand landing on top slab seed 0 | 20 ticks | 26 matches, zero divergences, exact 9-tick entity trajectory, y=78 placement, and stability queue | source removal plus placement above the unchanged top slab, 2 mutations and block light exact |
| falling sand failed placement on grass path seed 0 | 13 ticks | diagnostic ambient global cursors; exact 9-tick trajectory, tick-10 drop, and item Y/Y-velocity/health/age/pickup through tick 12; parked shaped-drop oracle strict over 44 aggregate updates | sole source removal, grass path unchanged, and all captured blocks and block light exact |
| falling sand failed placement on soul sand seed 0 | 13 ticks | diagnostic ambient global cursors; exact 10-tick trajectory, tick-11 drop, and item Y/Y-velocity/health/age/pickup through tick 12; parked shaped-drop oracle strict over 66 aggregate updates | sole source removal, soul sand unchanged, and all captured blocks and block light exact |
| falling sand failed placement on enchanting table seed 0 | 13 ticks | diagnostic ambient global cursors; exact 10-tick trajectory, tick-11 drop from y=77.75, and item Y/Y-velocity/health/age/pickup through tick 12; parked shaped-drop oracle strict over 88 aggregate updates | sole source removal, enchanting table unchanged, and all captured blocks and block light exact |
| falling sand failed placement on supported carpet seed 0 | 16 ticks | diagnostic ambient global cursors; exact 12-tick trajectory, tick-13 drop from y=77.0625, and item Y/Y-velocity/health/age/pickup through tick 15; parked shaped-drop oracle strict over 114 aggregate updates | sole source removal, carpet and stone support unchanged, and all captured blocks and block light exact |
| falling sand replacement of one-layer snow seed 0 | 17 ticks | 26 matches, zero divergences; exact 12-tick trajectory, tick-13 snow replacement, no item or Math draw, and `+2` stability callback; parked oracle covers both falling identities and all eight snow metadata values | source removal plus 78:0-to-12:0 replacement, stone support unchanged, and all captured blocks and block light exact |
| falling sand failed placement on eight-layer snow seed 0 | 13 ticks | diagnostic ambient global cursors; exact 10-tick trajectory, tick-11 drop from y=77.875, and exact item lifecycle through tick 12; parked snow oracle strict over 302 aggregate updates | sole source removal, snow 78:7 and stone support unchanged, and all captured blocks and block light exact |
| falling sand failed placement on dry farmland seed 0 | 13 ticks | diagnostic ambient global cursors; exact 9-tick trajectory, tick-10 drop from y=77.9375, and exact item lifecycle through tick 12; parked oracle covers all eight moisture values | sole source removal, farmland 60:0 and stone support unchanged, and all captured blocks and block light exact |
| falling sand failed placement on wet farmland seed 0 | 13 ticks | diagnostic ambient global cursors; same exact tick-10 branch with moisture metadata 7 retained and no trampling | sole source removal, farmland 60:7 and stone support unchanged, and all captured blocks and block light exact |
| falling sand failed placement on whole cake seed 0 | 13 ticks | diagnostic ambient global cursors; exact 11-tick trajectory, tick-12 drop from y=77.5, and exact item lifecycle at tick 12 | sole source removal, cake 92:0 and stone support unchanged, and all captured blocks and block light exact |

The full shared runtime now exposes and gates player physics, look, vitals,
fire, XP, fall distance, flags, held item, hurt timer, potions, death,
inventory, nearby entities, air, and the frozen clear-weather clock. Attack
cooldown is now modeled and gated; player death timer remains the one explicit
unsupported subfield. The exact pending-update subset covers inert stone plus
two generations of horizontal and one-block downward water flow in a bounded
two-air-layer basin over a flat stone floor, two deterministic generations of
flat Overworld lava, and downward lava reacting with an enclosed water source.
It also covers metadata-0 sand falling through a clear air column onto stone,
including the transient entity and the landed block's +2 stability update.
The capsule restores both Java's internal 48-bit `java.util.Random` cursor and
the signed 32-bit `World.updateLCG` cursor. A controlled server-thread callback
covers complete wheat growth semantics, while a separate isolated fixture
promotes the target's already-loaded player-chunk entry to rank zero and lets
the ordinary selector choose age-zero cactus. General loaded-world random-tick
membership and iteration are not yet claimed.
Dry NORMAL-difficulty fire on a stone platform now restores its
pending callback context and matches both a controlled random callback and a
real delayed scheduled dispatch, including ordered self/child rescheduling.
The bounded capsule also transports Java's source-column high-humidity
predicate. It applies the vanilla `-50` direct denominators and halves the
volumetric spread threshold without inferring biome humidity from C worldgen.
The same capsule transports `doFireTick=false`; its outer callback guard drains
due work without block, queue-successor, or callback-RNG effects.
The first redstone slice adds edit-driven six-neighbor notifications,
redstone-block, floor-lever, and floor-button weak power, immediate lamp
power-on, the vanilla four-tick lamp-off delay, and exact lit-lamp callback
restore from a capsule proof region. A capsule-restored powered stone button
also matches its 20-tick release and hands off to the lamp's independent
four-tick callback. Saved stone buttons now preserve all six support
orientations (DOWN/UP/NORTH/SOUTH/WEST/EAST), notify both themselves and the
correct attached normal-cube support, and reproduce the same release-to-lamp
handoff. Lever and stone/wood button neighbor updates now validate the exact
stored attachment even when another face has a valid support. Invalid support
drops normalized item 69:0, 77:0, or 143:0 with exact cursors, notifies the
powered support, and leaves an already-saved button callback to drain as stale
work. Forge directional side solidity covers slab/stair halves, snow layers,
farmland, and hopper tops; full fixed-item-pool rejection is atomic. A saved
powered stone pressure plate now performs the equivalent
entity-sensitive callback:
an unoccupied plate releases at +3, notifies both itself and the normal-cube
support below, and hands the lamp its independent +4 callback. Live player
crossings now use the plate's null collision box and exact inset,
quarter-block-high trigger AABB. Packet-driven movement activates at the Java
server phase, and an initially parked player activates during the ordinary
entity pass; both create the exact absolute +20 callback. A player remaining
on the plate through its due tick keeps metadata 1 and reschedules another
+20 check. Living mobs now take the same ordinary post-move block-collision
path. The deterministic proof uses an AI-enabled pig with its tasks, gravity,
and speed removed: it stays motionless while vanilla still executes
`move(0,0,0)` and `doBlockCollisions`. A true NoAI pig remains a negative
control and does not activate an unpowered plate. Pressure plates provide
direct weak power and upward strong power
through their support. Wooden plates now extend the same collision/callback
lifecycle with `Sensitivity.EVERYTHING`: an exact gravity-free EntityItem
activates block 72 at observation 0, retains its authoritative EID, stack,
pose, motion, age, and infinite pickup delay for 22 ticks, and causes the due
callback at observation 20 to enqueue the next +20 check. The identical item
over block 70 is a native negative control, preserving stone's MOBS-only
sensitivity. All four pressure-plate types now validate their floor support on
neighbor updates. Java's exact predicate accepts stateful fully opaque blocks
or any fence; invalid support emits normalized item 70:0, 72:0, 147:0, or
148:0, preserves powered notifications and stale callbacks, and rejects
atomically when the fixed item pool is full. Lever, button, and plate opacity
is zero, so emitted
lamp light propagates through them exactly. Bounded, air-covered,
stone-supported dust components converge only when reached by an edit: one
through sixteen-block attenuation, a T-branch, closed-loop drain, and
one-block climbs/descents are exact. Powered levers strongly power an attached
stone in all six directions;
powered wire above stone and a lit torch below stone now expose their
directional strong outputs too. Dust source removal disables wire output
during attenuation, drains without self-power, and notifies the indirect
consumer before its exact +4 off delay. Lit-torch add/remove and scheduled
state changes now issue the complete bounded second notification ring, so
direct removal and the +2 torch-to-+4 lamp handoff are exact. A read-only Java
registry capture records normal-cube, full-cube, opaque-material, and
power-provider masks for all 256 legacy block IDs and 16 raw metadata slots;
151 invalid metadata combinations remain explicit instead of being silently
canonicalized. The C runtime and capsule now consume the provenance-locked
normal-cube/power-provider masks for indirect power, saved-lamp proof regions,
and bounded wire climb/descent topology. Oak-plank on/off/save and climb
fixtures are exact; a negative fixture also preserves vanilla's distinction
that a redstone block beside a normal cube does not strongly power through it.
Non-normal support rules, other producers, active control orientation
interactions, and moving components remain unclaimed. The
floor-mounted torch inverter now includes its stateful
clock behavior: support power changes enqueue the exact +2 edge, lit torches
emit block light 7, both edge directions survive capsule restore, and a
rolling per-world toggle log burns out on the eighth off transition within 60
ticks. Burnout consumes the exact two-float/fifteen-double Java RNG sequence,
queues recovery at +160, ignores an attempted +2 relight while that update is
pending, prunes the stale history, and relights on the recovery callback. Wall
orientations 1/2/3/4 are exact in both directions. The state schema exposes the
otherwise invisible chronological toggle list, and a checkpoint
continuation restores a seven-toggle prefix before reproducing the eighth
toggle's burnout, +160 queue, blocks, and all 10,625 light cells.
The first live-light fixture places glowstone on the real server thread at
tape tick 0 and compares the parked `EnumSkyBlock.BLOCK` cuboid directly:
source 0-to-15, an adjacent 14, and every one of 10,625 cells match. The
complementary removal fixture starts with that exact field, removes glowstone
at tick 0, and drains all cells to zero on both engines. An opacity fixture
also inserts stone beside the live source: the blocked cell falls 14-to-0 and
the cell behind reroutes 13-to-11 exactly. A second-source fixture merges two
fields exactly, changing 1,304 sampled light cells. A source at local x=15
propagates 14 into the immediate x=16 neighbor with more than 2,000 exact,
non-vacuous changes; the exact total can vary with natural leaves at the far
cuboid edge.
The capsule also restores Java's saved `SkyLight` nibbles for the sampled
cuboid. Live opacity edits use Minecraft's bounded radius-17
`checkLightFor(SKY)` darken/brighten queue and column-height update instead of
converging an entire dirty chunk. Roof addition, removal, and a local-x15
boundary case each change exactly nine cells and match all 10,625 sampled
skylight values.
The narrow player tracer remains as `c_state_small.jsonl`; it is not the state
gate.

The gate is:

```bash
cd c/magma
uv run --no-project python trace/run_oracle_matrix.py --instances 2
```

Expected: every case reports `Overall=pass`, `State gate=pass`, and
`Block gate=pass`.

## Ordered queue

Status values are `DONE`, `ACTIVE`, `QUEUED`, and `BLOCKED`.
Effort is implementation plus oracle/test work for one experienced engineer.

| Order | ID | Status | Work | Narrow acceptance | Effort |
|---:|---|---|---|---|---|
| 0 | H-01 | DONE | Isolated two-client Java oracle pool | unique display, port, run dir, save dir; independent fresh resets | 1-2 d |
| 1 | H-02 | DONE | Raw pre/post block cuboids and strict transition comparator | identity plus id/meta/baseline/transition negative controls | 1-2 d |
| 2 | H-03 | DONE | Remove teleport velocity contamination | grounded Java and C both retain exact `vy=-0.0784000015258789` | 1 d |
| 3 | H-04 | DONE | Verified platform fixture and clean headroom | numeric server staging, support-cell read-back, six cleared air layers cover the complete fire proof neighborhood, no setup death or terrain collision | 1 d |
| 4 | H-05 | DONE | Exact client-step oracle mode | `player_ticks_existed` advances exactly one for every tape row; skipped rows abort | 1-2 d |
| 5 | H-06 | DONE | Promote movement and mining matrix to strict state gates | both random seeds and mining have zero observed-feature divergence | 1 d |
| 6 | P-01 | DONE | Establish machine-local performance floors | CPU, batched GPU, and 1080p CUDA medians captured; guard fails below 95% | 1 d |
| 7 | O-01 | DONE | Replace tracer blind spots with full-runtime observations | air, fire, XP, combat timers, potion list, and nearby entities are values or explicitly unsupported subfields, never blanket `null` | 3-7 d |
| 8 | O-02 | ACTIVE | Versioned state capsule/load contract. Player, inventory, block cuboid, represented entities, dimension, time/weather, and relevant RNG cursors restore on both sides; an unopened NoAI villager additionally restores profession, age, lazy economy state, living-sound timer, and private RNG/Gaussian state exactly across save/reload and 20 ticks | initialized villager economy, general entity/task NBT, arbitrary cross-feature load order, and exhaustive capsule translation remain | 1-2 wk |
| 9 | S-01 | DONE | Core survival fixture pack | drowning/air, burning/extinguish, XP pickup, melee cooldown/hurt/death, potion duration | 1-2 wk |
| 10 | W-01 | ACTIVE | World-tick fixture pack | water/lava, falling block, crop/random tick, fire spread, light updates; exact N-tick raw cuboids | 2-4 wk |
| 11 | B-01 | DONE | Fix known Nether still/flowing lava ID inversion | primary-source registry assertion, all four nether-full seed volumes, and CPU/CUDA agree | 1 d |
| 12 | B-02 | DONE | Complete double-height plant models: all six lower/upper texture pairs, grass/fern tint only, contextual upper actual state, and exact sunflower head geometry | jar-model, model-table, and emitted-mesh gates pass; six-species Java gallery has exact physics/world state over 1,188 ticks and pixel gate passes over 119 frames; no opaque slab; GPU 1 performance passes at 4,780 scalar steps/s, 2.93M Blaze env-ticks/s, and 31.37 CUDA fps | 2-5 d |
| 13 | B-03 | DONE | Port live blaze attack-step/on-fire state. Exact 60-tick windup, three six-tick shots, 78-on/100-off charged cycle, target reset/reacquire, six-point melee, five-point fireball hit plus five-second ignition, two-Gaussian blaze aim plus independent three-Gaussian fireball acceleration, exact floating motion, live burning render flags, non-explosive block impact, block-before-entity segment ordering, expanded player/mob/boat AABB hits, shooter exclusion through air tick 24, ordinary and custom stair/moving-piston block rays, the living-shooter `mobGriefing` branch, and represented dragon-part/crystal candidates | real-Java idle fixture confirms cadence and passes the 41-frame pixel gate; 17-line Java/CPU/CUDA entity-RNG gate, eight-tick live Java-vs-C fireball trajectory gate, native exact-cycle/damage/impact/ray/gamerule/floating/multipart gates, aggregate runtime, and GPU 1 performance guard all pass | 3-7 d |
| 14 | R-01 | ACTIVE | Redstone scheduling foundation | neighbor notification order, scheduled ticks, power query, save/reload of pending work | 2-4 wk |
| 15 | R-02 | ACTIVE | Dust, torches, levers/buttons/plates, lamps | line decay, torch burnout, edge cases, block states after every tick | 3-6 wk |
| 16 | R-03 | DONE | Repeater/comparator core and overrides, live trapped viewer power, and observers | diode timing/modes/directions/tile save, promoted static/entity sources, and the complete six-face observer pulse lifecycle are exact; broader command/item-frame lifecycle remains owned by later bundles | 3-5 wk |
| 17 | R-04 | ACTIVE | Pistons and movable-block rules; exact represented power and scheduling, six-facing normal/sticky extension/retraction and repower boundaries, bounded slime and 12-block traversal, broad mobility/DESTROY/drop/NBT coverage, represented player/item/mob pushes, moving-tile save/reload, and exact non-full-cube collision families through connected chorus plant, farmland, grass path, redstone diodes, brewing-stand base/stem, piston bases, closed shulker boxes, single slabs, carpet, snow layers, cake, beds, daylight detectors, end portal frames, ender chests, trapdoors, cauldrons, hoppers, flower pots, and cactus; ordinary player collision for these promoted surface shapes is shared by CPU/CUDA, and cactus contact damage/hurt immunity is exact; 576-case behavior/raw promotion, 568 strict state rows plus eight diagnostics; 33 later strict collision, ordinary-physics, and potion candidates through Health Boost await clean performance; performance passes at 4,775 scalar steps/s, 2.93M Blaze env-ticks/s, and 30.67 CUDA fps | additional removal/repower event boundaries, remaining entity-trigger coverage, broader slime and paired/cascading/randomized terminal attachments, mixed deferred callbacks, remaining non-full-cube shapes, ordinary physics beyond the promoted surface set, additional mob pushing, and rendering | 4-8 wk |
| 18 | R-05 | ACTIVE | Automation and rails. Live five-slot hoppers now push, pull, capture item entities, respect power and exact eight-tick cooldowns, and chain into adjacent inventories. Droppers insert into facing inventories. Twelve dispenser behavior classes now match Java position, motion, events, item mutation, and stable RNG cursor: default ejection, arrows, splash/lingering potions, fire charges, fireworks, oak boats, water/lava buckets, primed TNT, eggs, snowballs, experience bottles, and flint-and-steel air/failure/TNT paths. Rideable, TNT, and hopper minecarts follow straight/sloped rails; powered rails accelerate/brake, detector rails power, activator rails ignite TNT/disable hoppers, and hopper carts capture items | remaining item-specific dispenser behaviors and item/boat variants, hopper sided-inventory and double-chest edges, exact random occupied-slot selection under multi-slot inventories, curves/derailment/entity collisions/riding, furnace/chest/spawner/command-cart behavior, destruction/drops, broad save/load, rendering, and exhaustive comparator/rail notification edges | 6-12 wk |
| 19 | G-01 | DONE | Promote simple dungeons and abandoned mineshafts into live generation. Dungeons retain generated chest facing, exact placement-stream loot seed/table, and Forge-weighted spawner type. Normal and Mesa mineshafts retain exact recursive pieces, block placement, rails/webs/fences, deferred chest-minecart loot seeds, cave-spider spawners, and distinct live cave-spider type | exact Java/CPU dungeon volumes and loot; exact Java/CPU/CUDA mineshaft topology, 138,240 placement states, and 868 loot fields; natural live dungeon and seed-143 mineshaft chest/spawner fixtures; aggregate native gates; no idle-tick work | 1-3 wk |
| 20 | F-01 | ACTIVE | Weather bundle. Exact Java/CPU/CUDA timer and rain/thunder strength transitions, interpolated sky/fog/lightmap/celestial attenuation, real rain/snow textures, Java-locked precipitation geometry, open-sky player wet/fire extinguishing, lightning spawn/lifecycle/events/fire/strikes and represented mob conversion, plus ice/snow/cauldron precipitation callbacks are implemented | pixel-locked lightning-bolt visuals, weather audio playback and particles, complete biome-height/loaded-area precipitation edges, and real-Java weather pixel-tape promotion | 3-6 wk |
| 21 | F-02 | ACTIVE | Enchanting bundle. Exact bookshelf scan, seeded offer recomputation, item/lapis slots, XP/lapis costs, option application/reseed, book conversion, and a playable live table UI are implemented | anvils, complete enchanted-book combination paths, enchantment effects beyond represented combat/tool paths, runic text/glint, persistence edges, and real-Java UI pixel promotion | 4-8 wk |
| 22 | F-03 | ACTIVE | Brewing bundle. Five-slot live stands, exact fuel/timer and all 1.11.2 registry recipes, bottle filling, drinkable potion and milk completion, player effects, save/capsule state, break drops, GUI progress, comparator fullness, exact seeded splash/lingering launch, player impact scaling, instant player/mob effects with undead reversal, water-fire extinguishing, water damage to blazes/endermen, exact event colors, per-target lingering-cloud lifecycle/reapplication, cold scalar-state fixture resume for thrown potions/clouds, and bounded mob status-effect storage/combine/aging with live regeneration, poison, undead applicability, fire resistance, Speed, Slowness, Strength, Weakness, Jump Boost, post-immunity Resistance, periodic Wither death/drop, Health Boost, Absorption, Levitation, Water Breathing plus represented general-mob air/drowning, normal-survival Invisibility model/gel suppression with independent fire rendering, exact player Night Vision lightmap/fog rendering, exact player Blindness fog plus sprint-start suppression, and the exact represented critical-hit/base-damage/ordinary-sprint-enchantment-knockback/Fire-Aspect boundary are implemented | remaining mob world/render and non-brewing effect behaviors, player-hit sounds/particles/sweep/Thorns/statistics and player-target velocity acknowledgement, remaining projectile direct-hit edges, cloud particles, automatic Java capsule translation and full NBT persistence for projectile/cloud state, custom potion NBT, automation-side insertion/extraction, and remaining real-Java GUI/potion pixel promotion | 3-6 wk |
| 23 | F-04 | ACTIVE | Breeding bundle; exact breeding-item feeding, growth, love, status, and hand order are promoted for represented sheep, cows, pigs, and chickens; cow milking is exact through adult/creative/child and main/offhand routing, both player-position sounds, stacked-bucket insertion, full-inventory toss RNG/EID/item state, and the runtime item's same-boundary tick; adult/creative/child pig saddle application, immediate mount association, bounded flat-stone carrot-stick steering/boost, persistent step/wall/gap/slab ridden travel, continuous water travel plus bounded level, flowing, and falling dry-to-water entry, discriminating water/lava edge climb, still-lava travel and dry-to-lava entry, water/lava overlap precedence, authoritative sustained lava damage/fire/timer/RNG contact, exact vehicle-packet cactus/fire/lava including combined ordering and wet burning cleanup plus automatic represented zero-delta contact dispatch, bounded dry horizontal/vertical direct packet acceptance and wall/floor/ceiling/speed rollback, full-payload independent-server runtime acceptance/correction observation, bounded dry post-packet authoritative base state, resolved first-packet moving cactus/fire/lava plus asymmetric water entry/exit contact state, same-epoch horizontal/vertical two-packet and four-packet accept/collision/speed/accept tracker sequencing, per-server-tick tracker reseed, later/pre-ticked water-entry resetHeight RNG, real client correction pose/AABB application with immediate acknowledgement, and a bounded runtime correction/prediction two-entry FIFO, ridden-server Fire Resistance rejection/expiry across packet fire, ON_FIRE, and lava, soul-sand/web contact, bounded north-ladder contact/climb, and bounded slime landing/bounce plus low-speed walking damping, bounded full-stone/water plus isolated stone/slab/snow/water explicit dismount placement, ordinary player-melee hit/death/drop construction, the retained moving deathTime 0..19 passenger window, and exact deathTime-20 terminal relocation are promoted through delayed main/offhand routing, inventory/durability, private RNG, boost curve/expiry, raw movement/pose/limbs/passenger state, exact knockback/sound/status and item construction, feed/name-tag precedence, already-saddled and sneaking rules, state-aware candidate collision, exact lower `isSideSolid(UP)` support, solid-box occlusion, the fixed nine-candidate dismount scan and pig-top fallback, pork-before-saddle item order, saddle independence from `doMobLoot`, and the real pig-then-passenger-item world-update order; both the immediate direct callback and an isolated real `World.updateEntities` birth boundary now match for all four species through live mate-task selection/update 60, child type/age/pose, parent cooldown, heart particles, XP, IDs, shared cursors, transported newborn private RNG/Gaussian state, same-boundary newborn tick, overlapping pushes, and each species' first unobstructed grounded PathPoint/MoveHelper step; chicken additionally matches constructor egg timer, adult/child timer ordering, flap state, airborne fall damping, and the isolated adult timer-expiry egg sound/item boundary through exact Math/private RNG, EID allocation, item construction, and same-boundary item tick; sheep genetics, two simultaneous births, persistent pre-existing living/XP interleaving, slot reuse, and capsule order remain exact | broader shaped and dynamic dismount layouts, non-player-source pig hit/loot construction, broader authoritative pig base ticks including divergent dual-pose water/lava and full bookkeeping, long mixed-axis and repeated-correction packet bursts, broader correction scheduling, general mob potion/effect storage, contact death/drop, emitted effects, and remaining movement-media/contact callbacks, slime sneaking/nonliving/mixed-contact variants, and ladder variants, client boost notification/render state, enchanted-stick NBT, saddle visuals, custom named-tag effects, accepted tempt/follow-parent/wander/watch, obstacle/gap and multi-tick persistent navigation, multi-animal and global ordering across item/TNT/projectile and other runtime stores, player statistics/criteria, broader persistence, and task conflicts | 2-4 wk |
| 24 | G-02 | ACTIVE | Villages. Exact spacing candidates, recursive piece graphs, orientation/biome variants, ground alignment, roads, wells, farms/crops, houses, doors, blacksmith chest facing/seed/loot, and generated resident coordinates/professions feed live population. Normal residents materialize once into the bounded mob store, use profession skins and the exact nine-part villager model, and retain lazy private-RNG merchant state. All 22 ordinary initial offers across 11 tested career selections match Java price rolls and recipe fields; matching, reversed two-input execution, use limits, pitch, XP, wealth, and first-use reset/willingness state are live. An unopened NoAI villager now round-trips through the shared state capsule and continues for 20 exact ticks, including its otherwise hidden living-sound RNG consumption | zombie-villager materialization, villager task AI/navigation, village door/reputation state, breeding, golems, enchanted-book/item and treasure-map offers, later career tiers/restocking, interactive merchant container/UI, initialized economy persistence, arbitrary chunk-load-order persistence, and pixel promotion | 8-16 wk |
| 25 | G-03 | ACTIVE | Scattered features, monuments, and woodland mansions. Desert pyramids, jungle temples, and swamp huts now use exact four-facing 1.11.2 structure pieces in live population. Pyramid and temple chest/dispenser seeds, facing metadata, exact loot tables, generated dispenser realization, traps/puzzles, swamp empty-pot tile quirk, and the one-time witch site are retained | runtime witch materialization/AI and one-time persistence, complete generated-block models/pixels, locate/persistence and arbitrary chunk-load-order edges, igloos, ocean monuments/guardians, woodland mansions/illagers, and their loot/entity lifecycle | 8-16 wk |
| 26 | E-01 | ACTIVE | Outer End terrain plus exact chorus/island/gateway population primitives, bounded live chunk-discovery population, deterministic recursive End-city graphs, real template placement, ships, elytra frames, gateway travel, and exact End-city treasure realization are implemented | arbitrary chunk-load-order RNG/persistence, shulkers, exhaustive population edges, exact chorus internal-face culling, and End pixel promotion | 6-12 wk |
| 27 | E-02 | DONE | Elytra acquisition and fireworks: ship elytra, route recipes, exact rocket constructor/first motion, boost, explosion damage, and durability are implemented | focused Java/native acquisition, motion, boost, explosion, and durability gates pass; cosmetic particles and custom-NBT rendering remain V-01 work | 3-6 wk |
| 28 | L-01 | ACTIVE | Fishing cast/bobber state, water/block/item/living collision, bite timing, open-water/weather/luck/lure modifiers, reeling, rod damage, XP/item emission, hook event state, a complete cold hook-state restore boundary, the real nested junk/treasure/fish loot table with damage/enchantments, and Java's exact 17-point first-person line centerline are implemented | partial-tick hook endpoint interpolation, exhaustive hooked-entity/collision and owner-removal edges, automatic full-capsule/NBT translation, custom loot NBT, and fishing-line pixel promotion | 3-6 wk |
| 29 | L-02 | ACTIVE | Remaining pets and animal life cycles; exact sheep shearing, grazing/regrowth, natural fleece selection, and child genetics are promoted; cow milking, pig saddle application, mount association, bounded carrot-stick steering/boost, persistent step/wall/gap/slab ridden travel, continuous water travel plus bounded level, flowing, and falling dry-to-water entry, discriminating water/lava edge climb, still-lava travel and dry-to-lava entry, water/lava overlap precedence, authoritative sustained lava damage/fire/timer/RNG contact, exact vehicle-packet cactus/fire/lava including combined ordering and wet burning cleanup plus automatic represented zero-delta contact dispatch, bounded dry horizontal/vertical direct packet acceptance and wall/floor/ceiling/speed rollback, full-payload independent-server runtime acceptance/correction observation, bounded dry post-packet authoritative base state, resolved first-packet moving cactus/fire/lava plus asymmetric water entry/exit contact state, same-epoch horizontal/vertical two-packet and four-packet accept/collision/speed/accept tracker sequencing, per-server-tick tracker reseed, later/pre-ticked water-entry resetHeight RNG, real client correction pose/AABB application with immediate acknowledgement, and a bounded runtime correction/prediction two-entry FIFO, ridden-server Fire Resistance rejection/expiry across packet fire, ON_FIRE, and lava, soul-sand/web contact, bounded north-ladder contact/climb, bounded slime landing/bounce plus low-speed walking damping, and full-stone/water plus isolated stone/slab/snow/water explicit dismount placement, ordinary player-melee hit/death/drop construction, retained moving ridden deathTime progression and exact terminal relocation, breeding-item feeding/growth/love, direct and live mating/birth, newborn private-state transport, same-boundary newborn continuation, and the first unobstructed grounded mating step are exact for represented sheep, cows, pigs, and chickens, including chicken flap/fall/timer state, the isolated adult egg-lay sound/item threshold, the pig's fixed nine-candidate dismount scan, state-aware collision and lower-support rules, exact knockback/sound/status/item state, pork-before-saddle death ordering independent of `doMobLoot`, and the terminal pig-then-passenger update order | tame/sit/follow/teleport, ownership, broader shaped/dynamic pig dismount layouts, non-player-source pig hit/loot construction, broader authoritative pig base ticks including divergent dual-pose water/lava and full bookkeeping, long mixed-axis and repeated-correction packet bursts, broader correction scheduling, general mob potion/effect storage, contact death/drop, emitted effects, and remaining ridden movement-media/contact callbacks, slime sneaking/nonliving/mixed-contact variants, and ladder variants, client boost notification, enchanted-stick NBT, saddle visuals, multi-animal and global entity-list ordering, obstacle/multi-tick navigation, full passive spawn-pack ordering, and conflicts with unrepresented swim/tempt/follow tasks remain | 4-8 wk |
| 30 | A-01 | ACTIVE | One ordered fixed-size sound-event stream now unifies represented mob/item sounds, world events, lightning, firework launch, fishing bite, and villager trade. An owned-asset generator resolves 58 Minecraft sound events into 134 weighted OGG variants. Interactive play predecodes them once and consumes the stream through a fixed 32-source OpenAL pool with position, attenuation, volume, and pitch; headless/RL paths initialize no audio and do no playback work | exact Java variant-selection cursor and category sliders, music and ambient loops, records, subtitles/options, remaining gameplay emitters, block-break material resolution, particle-coupled RNG events, firework blast/distance/reverb behavior, device hotplug, and audio output comparison | 4-10 wk |
| 31 | V-01 | QUEUED | Pixel-exact residual queue | hand poses, preview LSBs, entities/particles, portal/water, fog/lightmap, remaining canonical tape clusters | ongoing |

The order is intentional. Harness and observability come before new behavior so
we do not create unmeasured systems. The three small known bugs are next.
Redstone then gets the first major implementation slot, as requested. Existing
isolated kernels are promoted before greenfield side structures. Audio mixing
is late, but gameplay systems must retain sound-event emission and RNG side
effects as soon as those systems are represented.

### R-03 comparator override inventory

This list is derived from every 1.11.2 override of
`hasComparatorInputOverride` in `java/oracle-src`, plus
`BlockRedstoneComparator.findItemFrame`. It prevents “remaining containers”
from hiding distinct state, entity, and automation dependencies.

| Order | Source | Vanilla rule | Status / owning slice |
|---:|---|---|---|
| 1 | cake 92, cauldron 118, End frame 120 | metadata-derived static signal | DONE in R-03 |
| 2 | ordinary/trapped chest 54/146 | 27/54-slot fullness; trapped viewers are a separate power producer | DONE in R-03 |
| 3 | furnace 61/62 | three-slot fullness | DONE in R-03 |
| 4 | dispenser/dropper 23/158 | nine-slot fullness | DONE in R-03; reusable bounded inventory-tile pool established |
| 5 | jukebox 84 | inserted record index 1..12 | DONE in R-03; sound playback remains A-01 |
| 6 | command blocks 137/210/211 | last command success count | DONE in R-03 for the exact inert saved subset; command execution remains separate |
| 7 | item frame behind one normal cube | displayed-item rotation 1..8; empty is zero; exactly one matching frame | DONE in R-03 for exact empty/plain-stone saved comparator state; damage, drops, maps/tags, rendering, and broader lifecycle remain separate |
| 8 | brewing stand 117 | five-slot fullness | DONE in active F-03; live five-slot state, save/capsule restore, and comparator updates share one bounded tile |
| 9 | hopper 154 and detector rail 28 | five-slot fullness; rail additionally queries a qualifying minecart | ACTIVE in R-05; hopper inventory fullness and represented detector occupancy are live, broader cart/container edges remain |
| 10 | shulker boxes 219..234 | 27-slot fullness | QUEUED with E-01 End-city inventory/persistence |

### Active slice evidence

- F-03 now has a complete drinkable-potion vertical slice. The shared
  Java/CPU brewing battery covers every 1.11.2 item/type conversion, invalid
  input boundary, fuel load, 400-tick lifecycle, ingredient interruption,
  bottle bits, brew event, and dragon-breath container return. Live stands use
  the same five slots for GUI clicks, comparator fullness, break drops, and
  state-capsule persistence. Glass bottles raycast and fill from water without
  consuming it, including stacked and full-inventory outcomes; finished
  potion and milk use returns the correct container and drives the bounded
  player-effect list. Splash and lingering bottles now share the exact
  three-Gaussian throwable heading, entity-local RNG cursor, 0.99/0.8 drag,
  0.05 gravity, colored impact event, and item billboard. Player splash
  duration/instant scaling, water-fire extinguishing, and the lingering
  cloud's wait/shrink/scan/reapplication/radius-on-use lifecycle are live.
  Splash instant effects now include represented living mobs, exact undead
  reversal and healing caps; water damages nearby blazes/endermen. Lingering
  instant effects retain an independent reapplication deadline for every
  represented target. Non-instant effects now enter a bounded per-mob list
  with vanilla amplifier/duration combination and pre-decrement cadence.
  Splash duration rounding and lingering quarter-duration delivery are exact;
  regeneration, poison, undead rejection, duration-one fire resistance,
  Speed/Slowness movement, Strength/Weakness melee, Jump Boost, post-immunity
  Resistance, periodic Wither through ordinary death/drop, Health Boost,
  Absorption, Levitation, Water Breathing, and Invisibility execute live.
  Invisibility suppresses base model and slime gel geometry in the normal
  survival view, clears on expiry, and retains the separate fire overlay.
  Player Night Vision uses the exact duration/partial-tick warning flicker,
  applies normalization after each dimension provider's lightmap colors and
  after fogColor1 for clear, terrain, water, and lava fog, and feeds the same
  fixed 256-entry LUT to world, entity, hand, and particle rendering. The
  50-tick sealed-tunnel Java tape is physics-clean and passes state plus three
  non-stale pixel frames; the warning flicker has separate bit-exact numeric
  coverage because framebuffer partial ticks are not recorded in tapes.
  General represented
  living mobs carry raw reloadable air state, use their exact eye heights for
  water membership, reset to 300 when dry, and issue the 2-health drown pulse
  after 320 submerged ticks with the exact 48 bubble RNG draws.
  Cold JSONL fixtures can resume an in-flight potion from
  exact age/pose/motion or an active cloud from every represented lifecycle
  scalar; state output round-trips those fields and strict next-tick tests
  cover throwable motion and an age-10 cloud scan.
  Focused gates pass 378 Java/CPU brewing values, 162 Java/CPU crafting values,
  76 Java/CPU throwable/cloud/effect raw-state values, 375 live/runtime checks, all
  player-control checks, container/screen/capsule coverage, and the legal
  spawn-to-End route. Brewing is gated by `--brewing on`; disabled worlds do
  not enter its active tile loop. Remaining mob world/render and non-brewing
  effect behaviors, cloud particles, automatic Java capsule translation/full NBT
  persistence, automation faces, and the real-Java potion/GUI pixel promotion
  remain open.
- G-01 now has a complete simple-dungeon gameplay vertical slice. The existing
  exact `WorldGenDungeons` block path captures, without extra RNG draws, each
  placed chest's `nextLong`, corrected horizontal facing, and the final
  Forge `DungeonHooks` weighted mob roll. Fixed populate-window sidecars carry
  those cold tile values into live chunks; generated chest use and unopened
  break materialize `chests/simple_dungeon`, while nearby spawner discovery
  registers the captured skeleton, zombie, or spider type. The table embeds
  all three 1.11.2 JSON pools, including registry-order `EnchantRandomly` books.
  Java and CPU agree on 868 emitted slot/enchantment values over four seeds,
  all three positive dungeon block-volume seeds remain exact, and the natural
  seed-88 product fixture verifies chest metadata, loot determinism, and zombie
  spawner registration. Metadata storage is fixed and generation-cold; no
  tick-time allocation or ordinary idle-path work was added. Mineshafts remain
  the open half of G-01.
- L-02 now includes exact Forge shears interaction for represented sheep.
  Survival entity selection uses the three-block expanded-AABB target, rejects
  a nearer solid block, and queues the use packet to the following server
  tick. Main-hand and empty-main/offhand shears, adult eligibility, child and
  already-sheared handled no-ops, fleece metadata, Unbreaking III, durability,
  the neutral shear sound, and one-to-three separate wool entities all match
  Java. Exact item constructor state includes causal IDs, pickup delay,
  position, motion, yaw, hover phase, and the sheep, global Math, and injected
  local-Random cursors. Two consecutive seven-case gates pass at
  `trace/out/test_shearing_full_1.log` and
  `trace/out/test_shearing_full_2.log`; bounded native coverage also proves
  capacity refusal is atomic. The runtime, mob, and entity-render suites pass.
  The complete native aggregate passes in 5:29.76 at 446,192 KB peak RSS with
  zero major faults and zero swap at
  `trace/out/test_runtime_shearing_full.log`.
  With the oracle stopped, the scalar guard passes at 4,788 steps/s against
  the 4,062 baseline and 3,858.9 floor at
  `trace/out/perf_guard_shearing_cpu_1.json`. This transition is event-driven,
  scans only the fixed represented entity pool on a use edge, and adds no idle
  allocation or world scan. GPU 1 was untouched. Near-break client effects,
  and general main/offhand item-use precedence remain open; grazing, natural
  age progression, and fleece-color selection are covered below.
- L-02 now also includes the exact `EntityAIEatGrass` transaction for
  represented AI-enabled sheep. Adult and child eligibility consume one
  `Entity.rand.nextInt(1000/50)` draw before checking grass below or tallgrass
  meta one at the current floored position. Accepted tasks emit status 10,
  start at timer 40, update to 39 on the scheduler start tick, and apply at
  timer four on update 36. Tallgrass wins over grass below. `mobGriefing`
  gates only block mutation and world event 2001; wool regrowth and child
  `addGrowth(60)` still occur, adding 1,200 age ticks and clamping at zero.
  The natural path runs the per-entity three-tick goal scheduler, yields to
  represented panic, suppresses movement while active, advances passive age,
  and never runs the goal for `NoAI` fixtures. Live sheep views now carry the
  exact timer-derived head Y/X pose at tick boundaries instead of the old
  always-mid-graze idle approximation.
  Two consecutive 11-case Java-vs-magma gates pass in 3.2 seconds at
  `trace/out/test_grazing_full_1.log` and
  `trace/out/test_grazing_full_2.log`, each at 30,252 KB peak RSS with zero
  swap. The focused product path passes scheduler, regrowth, gamerule, event,
  pose, age, and panic controls at
  `trace/out/test_grazing_runtime.log`. The first complete native run caught
  and rejected a `NoAI` RNG contamination; after the guard, the aggregate
  passes in 5:23.18 at 445,928 KB peak RSS with zero major faults and zero
  swap at `trace/out/test_runtime_grazing_full_2.log`. The stopped-oracle CPU
  guard passes at 4,718 steps/s against the 3,858.9 floor at
  `trace/out/perf_guard_grazing_cpu_1.json`. GPU 1 was untouched. Natural
  spawn-pack ordering, other higher-priority task conflicts,
  partial-tick/client pixel verification, and general animal persistence
  remain open.
- L-02 now includes exact natural sheep fleece-color selection. The selector
  consumes `World.rand.nextInt(100)` and, only for the 18..99 branch, one
  `nextInt(500)`. Black, gray, silver, brown, white, and rare pink metadata
  all match, including exact one-draw/two-draw cursor advancement. The
  `onInitialSpawn` boundary preserves the sheared high bit and the ordinary
  passive-spawn path now uses the shared runtime World cursor rather than the
  synthetic entity RNG. Generic fixture construction and save reload remain
  white/explicit and never reroll.
  Two consecutive 13-comparison real-Java gates pass at
  `trace/out/test_sheep_color_full_1.log` and
  `trace/out/test_sheep_color_full_2.log`, each in about 2.2 seconds at 30,252
  KB peak RSS with zero swap. The focused product gate proves all six branches
  plus an actual passive sheep spawn at
  `trace/out/test_sheep_color_runtime.log`. The complete native aggregate
  passes in 5:50.27 at 445,892 KB peak RSS, zero major faults, and zero swap at
  `trace/out/test_runtime_sheep_color_full.log`. The stopped-oracle CPU guard
  passes at 4,598 steps/s against the 3,858.9 floor at
  `trace/out/perf_guard_sheep_color_cpu_1.json`. This adds no allocation or
  idle scan and at most two LCG advances per created sheep. GPU 1 was
  untouched. The surrounding route-level passive spawner still uses its
  documented approximate hash-based position/type selection, so complete
  `WorldEntitySpawner` pack ordering is not claimed. The global effort
  estimate stays approximately 16%.
- F-04 now includes exact represented-sheep feeding and the direct mate/birth
  callback. Wheat use
  through the real hand route covers adult love state and status 18, child
  `ageUp`, forced-age state, survival and creative consumption, delayed
  main/offhand ordering, and solid-block occlusion. The complete 16 by 16
  parent-color matrix matches the nine crafting recipes and exact fallback
  RNG cursor for both Boolean outcomes. The direct mate callback uses strict
  distance squared below nine and births on update 60. Birth resets
  parent love and age, creates the child at age -24000 with exact fleece,
  emits seven exact heart particles including the Gaussian cache state, and
  preserves child/XP ID and constructor RNG order. Forge cancellation,
  null-child ordering, `doMobLoot`, and bounded child/XP capacity fallbacks
  are covered. The ordinary product scheduler is native-tested for its
  three-tick cadence, nearest compatible represented mate, panic priority,
  mate-over-graze priority, and no same-setup-tick restart after a failed
  continuation. The isolated high-air full-tick boundary is now exact for one
  coincident compatible pair. A parked Java fixture runs the real
  `World.updateEntities` live-list loop, pins the valid newborn `Entity.rand`
  save state, and proves that the appended child and XP orb both receive their
  first update later in that same tick. Parent and child counters, positions,
  motion, ages, AI task counters, RNG/Gaussian state, particles, world/Math
  cursors, IDs, and append order match bit-for-bit for recipe/fallback color
  and `doMobLoot` on/off cases. The native product mirrors that bounded
  boundary with a birth-local queue; it does not yet claim general dynamic
  entity-list ordering.
  Two consecutive final-source 13-case feeding comparisons pass at
  `trace/out/test_sheep_feed_full_3.log` and
  `trace/out/test_sheep_feed_full_4.log`, including the Java-wrapped
  `Integer.MIN_VALUE` child-age boundary. Two consecutive 512-row genetics
  matrices pass in 2.14 and 2.13 seconds at
  `trace/out/test_sheep_genetics_full_1.log` and
  `trace/out/test_sheep_genetics_full_2.log`; 36 rows use a crafting recipe
  and every fallback row retains the exact World RNG cursor. Two consecutive
  final-source eight-case mating lifecycles pass in 2.30 and 2.25 seconds at
  `trace/out/test_sheep_mating_full_3.log` and
  `trace/out/test_sheep_mating_full_4.log`. The focused ordinary/runtime gate
  passes in 0.03 seconds at `trace/out/test_sheep_mating_runtime.log`. The
  final-source complete native aggregate passes in 5:47.68 at 446,060 KB peak
  RSS, zero major faults, and zero swap at
  `trace/out/test_runtime_sheep_mating_full_final.log`. With the oracle stopped,
  scalar throughput passes at 4,804 steps/s against the 4,062 baseline and
  3,858.9 floor at `trace/out/perf_guard_sheep_mating_cpu_final.json`. GPU 1 was
  untouched. The three-case full-tick comparison passes twice at
  `trace/out/test_sheep_mating_tick_full_1.log` and
  `trace/out/test_sheep_mating_tick_full_2.log`, then passes again after the
  legacy falling-anvil ordering reconciliation at
  `trace/out/test_sheep_mating_tick_after_anvil.log`. The final header-rebuilt
  comparison passes at `trace/out/test_sheep_mating_tick_final_source.log`.
  The focused
  ASan/UBSan runtime passes at
  `trace/out/test_sheep_mating_tick_sanitized_final.log`; that run also removed
  signed-left-shift UB from the shared exact `Random.nextLong` helper, whose
  17-line Java/CPU/CUDA oracle remains exact. The final header-rebuilt native
  aggregate passes in 6:17.39 at 446,552 KB peak RSS, one major fault, and zero
  swap at `trace/out/test_runtime_sheep_mating_tick_full_final_source.log`.
  With the oracle stopped, scalar throughput passes at 4,583 steps/s against
  the 4,062 baseline and 3,858.9 floor at
  `trace/out/perf_guard_sheep_mating_tick_cpu_final.json`. No CUDA/Blaze
  performance path changed; the small CUDA RNG parity gate used GPU 1 without
  disturbing the shared workload. The expanded seven-case full-tick gate adds
  an overlapping 0.25-block pair and airborne pairs at 1.0, 2.0, and 2.75
  blocks. It now proves PathNavigateGround's airborne no-body-movement result,
  exact sequential living-entity push arithmetic, fresh-child ungrounded
  state, and half-size child movement physics at
  `trace/out/test_sheep_mating_collision_final.log` and
  `trace/out/test_sheep_mating_collision_final_2.log`. The focused sanitizer
  passes at `trace/out/test_sheep_mating_collision_sanitized_final.log`.
  The final-source native aggregate passes in 5:43.70 at 446,296 KB peak RSS,
  zero major faults, and zero swap at
  `trace/out/test_runtime_sheep_mating_collision_final.log`, while stopped-
  oracle scalar throughput passes at 4,837 steps/s against the 3,858.9 floor
  at `trace/out/perf_guard_sheep_mating_collision_cpu_final.json`. Non-sheep
  breeding, accepted lower AI tasks and grounded/full navigation,
  simultaneous-birth loaded-list ordering, entity cramming, breeding
  statistics and criteria, and lifecycle persistence remain open.
  The ten-case grounded-navigation expansion retains every airborne and
  overlap control and adds axial, diagonal, and 90-degree-clamped grounded
  births. It matches the integer PathPoint center, 1.11.2 table-based
  `MathHelper.atan2`, `EntityMoveHelper.limitAngle`, and the exact double use
  of the sheep movement-speed attribute in its first travel step. The Java
  fixture pins shared JVM Math/EID cursors only at `spawnBaby` HEAD and asserts
  that hook exactly once, excluding unrelated integrated-client construction
  without hiding causal child/XP consumption. Full gates pass both before and
  after a cold oracle restart in 0.74 and 0.82 seconds at
  `trace/out/test_sheep_mating_grounded_navigation.log` and
  `trace/out/test_sheep_mating_grounded_navigation_restart.log`. The focused
  ASan/UBSan runtime passes at
  `trace/out/test_sheep_mating_grounded_navigation_sanitized.log`. The broad
  native aggregate passes in 6:40.87 at 446,224 KB peak RSS, zero major
  faults, and zero swap at
  `trace/out/test_runtime_sheep_mating_grounded_navigation.log`. Two
  stopped-oracle scalar guards pass at 4,339 and 5,018 steps/s against the
  4,062 baseline and 3,858.9 floor at
  `trace/out/perf_guard_sheep_mating_grounded_navigation_cpu.json` and
  `trace/out/perf_guard_sheep_mating_grounded_navigation_cpu_2.json`. The
  exact path runs only for an active grounded mate task, uses no allocation,
  and leaves CUDA/GPU paths untouched. Obstacle/gap paths, persistent
  multi-tick navigator state, head/look helpers, and general loaded ordering
  remain open.
  The twelve-case simultaneous-birth expansion adds two independent pairs in
  one real `World.updateEntities` boundary. Pair A alone establishes the
  fixture Math/EID cursor; pair B inherits the real child and optional XP
  constructor consumption, so the test does not encode the causal cursor
  twice. Both loot-enabled and `doMobLoot=false` rows match exact parent,
  child, and XP dispatch order, IDs, private RNGs, particles, first updates,
  and final shared cursors. The full matrix passes before and after a cold
  oracle restart at `trace/out/test_sheep_mating_simultaneous_order.log` and
  `trace/out/test_sheep_mating_simultaneous_order_restart.log`; the final-source
  rerun is `trace/out/test_sheep_mating_simultaneous_order_final.log`. The
  focused ASan/UBSan runtime passes at
  `trace/out/test_sheep_mating_simultaneous_order_sanitized.log`, and the broad
  native aggregate passes in 5:12.02 at 446,248 KB peak RSS, zero major faults,
  and zero swap at
  `trace/out/test_runtime_sheep_mating_simultaneous_order.log`. Two stopped-
  oracle scalar guards pass at 4,922 and 4,626 steps/s against the 3,858.9
  floor at
  `trace/out/perf_guard_sheep_mating_simultaneous_order_cpu_1.json` and
  `trace/out/perf_guard_sheep_mating_simultaneous_order_cpu_2.json`. The native
  scheduler uses a fixed bounded event tail and child-RNG FIFO with no heap
  allocation; GPU 1 was untouched.
  The fourteen-case persistent-order expansion adds an XP orb to Java's loaded
  list before the parents. Old C groups the living slots first and fails its
  first ordering assertion at
  `trace/out/test_sheep_mating_preexisting_xp_old_c.log`. The corrected native
  scheduler retains a fixed generation-tagged list across ticks, dispatches
  living and XP references in one order, invalidates expired entries, and
  safely reuses the same XP slot for the later birth orb. Full comparisons
  pass before and after a cold Java restart at
  `trace/out/test_sheep_mating_persistent_order_final.log` and
  `trace/out/test_sheep_mating_persistent_order_restart.log`. The focused
  native gate additionally proves two-tick persistence in both ordinary and
  controlled schedulers at
  `trace/out/test_sheep_mating_persistent_order_native.log`; ASan/UBSan passes
  at `trace/out/test_sheep_mating_persistent_order_sanitized.log`. The broad
  aggregate passes in 6:13.91 at 446,400 KB peak RSS, zero major faults, and
  zero swap at `trace/out/test_runtime_persistent_order.log`. Two stopped-
  oracle scalar guards pass at 4,936 and 5,108 steps/s against the 3,858.9
  floor at
  `trace/out/perf_guard_sheep_mating_persistent_order_cpu_1.json` and
  `trace/out/perf_guard_sheep_mating_persistent_order_cpu_2.json`. Storage is
  fixed, scans at most 380 tagged references, allocates nothing, and does not
  touch CUDA or GPU 1. Older multi-entity capsules did not carry Java's loaded
  order and must be regenerated; they are no longer silently reconstructed by
  EID. Items, TNT, projectiles, and other runtime pools remain outside this
  list.
  A fifteenth strict case starts a no-AI cow at health zero and death time 19
  before the sheep parents. Java removes it at death time 20, then appends the
  child and breed XP; native reuses the cow's living slot while its generation
  guard removes the stale reference. The exact matrix passes ten consecutive
  stress repetitions and after a cold restart, with the final run at
  `trace/out/test_sheep_mating_living_slot_reuse_final.log`. The focused native
  and ASan/UBSan gates pass at
  `trace/out/test_sheep_mating_living_slot_reuse_native.log` and
  `trace/out/test_sheep_mating_living_slot_reuse_sanitized.log`. The full
  aggregate passes in 5:07.96 at 446,216 KB peak RSS, zero major faults, and
  zero swap at `trace/out/test_runtime_living_slot_reuse.log`. Two clean scalar
  guards pass at 5,033 and 5,174 steps/s at
  `trace/out/perf_guard_sheep_mating_living_slot_reuse_cpu_1.json` and
  `trace/out/perf_guard_sheep_mating_living_slot_reuse_cpu_2.json`. The exact
  state API now accepts health zero, a valid serialized in-progress death
  state; this adds no tick-path work.
  Capsule import now preserves this represented living/XP order as well.
  Java records each captured entity's original `loadedEntityList` rank before
  retaining the existing distance sort and 64-entity observation cap. The
  neutral capsule validates complete, unique, non-negative ranks and emits
  exact NoAI-pig/XP spawn events in rank order rather than EID order. The
  discriminating selftest stores pig 91 before XP 92 in its distance-sorted
  payload but restores XP 92 before pig 91; duplicate, partial, and ambiguous
  legacy multi-entity order all fail closed. Its complete round-trip gate
  passes in 0.15 seconds at 31,704 KB peak RSS at
  `trace/out/state_capsule_loaded_order.log`. The focused native test uses the
  same reverse-EID order in both schedulers, retains it across tick one, then
  removes the age-6000 XP exactly on tick two; it passes in 0.17 seconds at
  63,560 KB at `trace/out/test_sheep_mating_capsule_order_native.log` and under
  ASan/UBSan in 0.77 seconds at 132,236 KB at
  `trace/out/test_sheep_mating_capsule_order_sanitized.log`. The 15-case exact
  Java comparison remains green at
  `trace/out/test_sheep_mating_capsule_order_final_2.log`. Two stopped-oracle
  scalar guards pass at 5,131 and 5,134 steps/s against the 3,858.9 floor at
  `trace/out/perf_guard_capsule_loaded_order_cpu_1.json` and
  `trace/out/perf_guard_capsule_loaded_order_cpu_2.json`. Capture/import is a
  cold bounded path, so this adds no per-tick or GPU work. The broad native
  aggregate also passes in 5:07.15 at 446,316 KB peak RSS, zero major faults,
  and zero swap at `trace/out/test_runtime_capsule_loaded_order.log`. Global
  cross-store entity ordering remains open.
  Breeding-item feeding is now exact for represented cows, pigs, and chickens
  as well as sheep. The shared interaction accepts every 1.11.2 species food,
  rejects foods from the other groups, and matches adult love, player credit,
  status 18, survival and creative inventory, child age-up/forced-age
  arithmetic, ordinary love ticking, damage reset, and delayed hand order.
  Two final 53-case real-Java comparisons pass in 2.23 and 2.30 seconds at
  30,252 KB peak RSS at `trace/out/test_animal_feed_final_2.log` and
  `trace/out/test_animal_feed_repeat.log`. Focused native and
  AddressSanitizer gates pass at `trace/out/test_animal_feed_native_final.log`
  and `trace/out/test_animal_feed_asan.log`; the existing sheep mating oracle
  remains green at `trace/out/test_animal_feed_sheep_mating_regression.log`.
  The broad aggregate passes in 5:10.07 at 445,584 KB peak RSS, zero major
  faults, and zero swap at `trace/out/test_runtime_animal_feed.log`. Two
  stopped-oracle scalar guards pass at 4,928 and 5,045 steps/s against the
  3,858.9 floor at `trace/out/perf_guard_animal_feed_cpu_1.json` and
  `trace/out/perf_guard_animal_feed_cpu_2.json`. The species switch runs only
  after an animal entity right-click, and love ticking reuses the existing
  passive loop without another scan or allocation. Adult-cow bucket and pig
  saddle routes conservatively preempt an incorrect offhand feed, while their
  pig saddle/mount effects and named tags remained open at that checkpoint.
  Cow milking is promoted by the later exact interaction slice. GPU 1 was
  untouched.
  The global effort estimate remains approximately 16%.
- F-04 now promotes the ordinary cow, pig, and chicken live mating boundary,
  not only its direct callback. A shared fixed-state scheduler selects the
  nearest compatible mate, reaches the real update-60 birth, appends child then
  XP in persistent order, and gives the newborn its same-boundary first tick.
  Chicken additionally restores constructor egg timers, five flap floats and
  jockey state, then runs its exact post-move flap/fall and adult-versus-child
  timer ordering. Two 19-case Java-vs-magma matrices retain all 15 mature sheep
  controls and pass in 1.13 and 1.02 seconds at 30,252 KB peak RSS at
  `trace/out/test_animal_live_scheduler_final_1.log` and
  `trace/out/test_animal_live_scheduler_final_2.log`. The 23-case direct matrix
  remains green at
  `trace/out/test_animal_live_scheduler_direct_regression.log`; focused native
  and ASan/UBSan gates pass at
  `trace/out/test_animal_live_scheduler_native.log` and
  `trace/out/test_animal_live_scheduler_asan.log`. Stopped-oracle scalar guards
  pass at 5,108 and 5,079 steps/s against the 3,858.9 floor at
  `trace/out/perf_guard_animal_live_scheduler_cpu_1.json` and
  `trace/out/perf_guard_animal_live_scheduler_cpu_2.json`. The broad native
  aggregate passes in 5:34.22 at 446,192 KB peak RSS, zero major faults, and
  zero swap at `trace/out/test_runtime_animal_live_scheduler.log` and `.time`.
  Five 96-entry float arrays plus one byte array add 2,016 bytes, no allocation,
  and no global scan. The isolated adult chicken timer-expiry boundary now also
  emits the exact sound, consumes private and shared Math RNG in Java order,
  allocates the egg EID, constructs its four randomized fields, and advances
  the appended item once in that same world boundary. One focused and two full
  20-case Java-vs-magma runs pass at
  `trace/out/test_chicken_egg_threshold.log`,
  `trace/out/test_animal_live_egg_full_1.log`, and
  `trace/out/test_animal_live_egg_full_2.log`; full-store native coverage proves
  bounded loss while preserving sound, cursor, RNG, and timer side effects.
  ASan/UBSan and the broad runtime aggregate pass at
  `trace/out/test_chicken_egg_asan.log` and
  `trace/out/test_runtime_chicken_egg.log`. Stopped-oracle scalar guards pass at
  5,115 and 4,952 steps/s against the 3,858.9 floor. The same live fixture now
  also promotes each non-sheep species' first unobstructed grounded mating
  step. Cow uses movement speed 0.20, pig and chicken use 0.25, and the
  mating-only MoveHelper applies that value to both `moveForward` and
  `landMovementFactor` without changing unpromoted wander/panic or CUDA code.
  Cow axial, pig diagonal, and chicken 90-degree-clamped rows expand the strict
  matrix to 23 cases; two full comparisons pass in 1.38 and 1.40 seconds at
  `trace/out/test_animal_grounded_geometry_final_1.log` and
  `trace/out/test_animal_grounded_geometry_final_2.log`. ASan/UBSan and the
  broad aggregate pass at
  `trace/out/test_animal_grounded_speed_asan_final.log` and
  `trace/out/test_runtime_animal_grounded_speed.log`; the latter takes 5:32.58
  at 446,068 KB peak RSS, zero major faults, and zero swap. Stopped-oracle CPU
  guards pass at 5,038 and 5,164 steps/s. Accepted lower tasks, obstacle/gap
  and multi-tick navigation, multi-chicken/global entity ordering,
  persistence, and statistics remain open; the global effort estimate remains
  approximately 16%.
- F-04 now promotes the ordinary player-melee pig death boundary and the
  moving corpse continuation. A normal-AI, moving, saddled, mounted real Java
  pig is lethally hit through `causePlayerDamage`; native matches status and
  death-sound order, knockback, recent-hit/player credit, raw/cooked pork
  count, `doMobLoot`, saddle ordering, exact item constructors, and private,
  world, Math, and EID cursors. Death ticks 1 and 19 match raw corpse motion,
  collision/friction, pig and player AABBs, passenger following, player
  motion/fall state, item physics, and pig-player-item update order. The new
  four-case strict gate is `trace/test_pig_lethal.py`; five repeated legacy
  drop matrices and three repeated lethal matrices also prove that client
  mirror constructors cannot contaminate the fixture's global cursors. The
  JDK 8 and product builds, focused native, all-source ASan, and affected pig,
  cow, sheep, and chicken anvil-lifecycle rows pass. The broad aggregate passes
  in 5:30.77 at 446,096 KB peak RSS, zero major faults, and zero swap. The
  stopped-oracle scalar guard passes at 4,771 steps/s against the 3,858.9 floor
  in `trace/out/perf_guard_pig_lethal_cpu_final.json`. The exact path adds no
  idle scan or allocation and GPU 1 was untouched. Non-player-source pig
  hit/loot construction, broader shaped dismount layouts, persistent obstacle travel,
  visuals, and persistence remain open; global effort remains approximately
  16%.
- F-04 now also promotes persistent client-authoritative ridden-pig travel
  through four bounded layouts. One-block step, two-block wall, two-cell gap,
  and bottom-slab fixtures retain the same client pig, player, arena, private
  RNG, and boost state for 48 ticks each. All 192 trace rows match Java raw
  position, motion, AABB, fall/contact state, pig pose/limbs, passenger state,
  boost cursor, and entity RNG exactly. The complete pig
  ride/dismount/death matrix grows from 32 to 36 strict cases.
  The first full-cube probe isolated Java's double-subtract/float-store fall
  rounding at tick 28. After that correction, the bottom-slab discriminator
  failed first at tick 10 because the old live-mob collector represented it as
  a full cube. Active ridden pigs now reuse the existing state-aware chunk
  collision collector and shared exact step solver; ordinary mobs retain the
  prior fixed Pcf path. Persistent fall distance and horizontal/vertical
  collision flags are stored per living slot. The collector uses a fixed
  trailing 512-AABB workspace that is overwritten on use and skipped by mob
  initialization, with no heap allocation or idle scan.
  The JDK 8 and product builds, CPU physics/entity-spine gates, CUDA
  compile-only entity-spine gate, focused native suite, two consecutive
  Java/native matrices, and all-source AddressSanitizer traces pass. The broad
  aggregate passes in 5:34.18 at 445,716 KB peak RSS and the already-built
  runtime alone passes in 4:53.18 at 252,968 KB, both with zero major faults
  and zero swap. The stopped-oracle scalar guard passes at 4,884 steps/s
  against the 3,858.9 floor in
  `trace/out/perf_guard_pig_obstacle_travel_cpu.json`. GPU execution remained
  untouched while GPU 1 was shared. Water/web/ladder/slime and remaining
  block-contact
  callbacks, server correction/packet timing, saddle rendering, and entity
  persistence remain open; global effort remains approximately 16%.
- F-04 now also promotes ridden-pig soul-sand contact through a six-tick
  persistent trace, paired with an otherwise identical stone-floor control.
  Before the fix, the soul-sand row first differed at tick 1 only in raw
  horizontal motion; position, vertical motion, geometry/contact state,
  passenger state, boost state, and RNG remained exact. The active ridden path
  now collects bounded soul-sand cells from the same cached chunk window and
  applies Java's per-overlapped-cell 0.4 horizontal multiplier after collision
  resolution and before final friction. The fixed contact-cell workspace is
  trailing scratch, overwritten on use, allocation-free, and skipped by mob
  initialization; ordinary mobs and the idle path are unchanged.
  The complete ride/dismount/death matrix grows from 36 to 38 strict cases and
  passes twice. JDK 8, product, focused mob/player, CPU physics/entity-spine,
  CUDA compile-only entity-spine, all-source AddressSanitizer, and the broad
  native aggregate pass. The broad gate takes 5:33.74 at 445,864 KB peak RSS,
  zero major faults, and zero swap. The stopped-oracle scalar guard passes at
  4,780 steps/s against the 3,858.9 floor in
  `trace/out/perf_guard_pig_soul_sand_cpu.json`. GPU execution remained
  untouched. Water, ladders, slime, other block-contact callbacks,
  server correction/packet timing, saddle rendering, and entity persistence
  remain open; global effort remains approximately 16%.
- F-04 now also promotes the persistent ridden-pig web latch through a
  six-tick cobweb corridor. Java's first move is unscaled, its final contracted
  AABB sets `isInWeb` and clears fall distance, and the following move consumes
  the latch with exact 0.25 X/Z and 0.05000000074505806 Y displacement factors,
  zeros stored motion, then relatches while overlap continues. The trace
  compares the latch explicitly in addition to raw movement, AABB,
  fall/contact, pose/limbs, passenger, boost, and RNG state.
  The exact-AABB move wrapper had bypassed the already-verified web consumer;
  the first candidate therefore failed at tick 1 in position, horizontal
  motion, passenger position, and limb state while the latch itself matched.
  Moving that existing consume operation to the exact wrapper boundary closes
  the measured cause. A single tagged fixed contact list now preserves Java's
  x/y/z callback order for both web and soul sand with one active-ridden scan,
  no heap allocation, and no idle-path work.
  The strict matrix grows from 38 to 39 cases and passes twice in 19.73 and
  20.00 seconds at 30,252 KB harness RSS. JDK 8, product, focused mob/player,
  CPU physics/entity-spine, CUDA compile-only entity-spine, and seven-layout
  all-source AddressSanitizer gates pass. The broad aggregate takes 5:18.13 at
  445,732 KB peak RSS, zero major faults, and zero swap. The stopped-oracle
  scalar guard passes at 4,815 steps/s against the 3,858.9 floor in
  `trace/out/perf_guard_pig_web_cpu.json`. GPU execution remained untouched.
  Water, slime, other callbacks, server correction/packet timing,
  saddle rendering, and entity persistence remain open; global effort remains
  approximately 16%.
- F-04 now also promotes bounded ridden-pig ladder contact and forward climb
  through paired six-tick clear and north-facing two-high ladder traces. The
  pig starts in a valid non-overlapping state 0.0125 blocks short of the
  ladder plane. Before the fix, the ladder row first differed at tick 0 only
  in vertical motion: Java applies the living-entity ladder clamps before
  movement, then a 0.2 upward impulse after horizontal collision and before
  gravity/drag. Position, AABB, collision, ladder state, passenger, pose, limb,
  boost, and RNG state already matched.
  The active-ridden path now uses the tagged contact list for the exact feet
  cell, applies Java's X/Z and descending-Y clamps before movement, resets fall
  distance, and applies the post-collision climb impulse at the measured
  boundary. This adds no scratch, allocation, idle scan, or ordinary-mob work.
  The strict matrix grows from 39 to 41 cases and passes twice in 21.51 and
  22.11 seconds at 30,252 KB harness RSS. JDK 8, product, focused mob/player,
  CPU physics/entity-spine, CUDA compile-only entity-spine, and nine-layout
  all-source AddressSanitizer gates pass. The broad aggregate takes 5:39.28 at
  445,380 KB peak RSS, zero major faults, and zero swap. The stopped-oracle
  scalar guard passes at 4,693 steps/s against the 3,858.9 floor in
  `trace/out/perf_guard_pig_ladder_cpu.json`. GPU execution remained untouched.
  Other ladder facings, vines, trapdoor ladders, side/top/falling contacts,
  clamp-limit cases, water, slime, other callbacks, server correction/packet
  timing, saddle rendering, and persistence remain open. Global effort remains
  approximately 16%.
- F-04 now also promotes bounded non-sneaking ridden-pig slime landing and
  bounce through paired six-tick stone/slime traces. Both begin 0.5 blocks
  above the support surface, airborne, with vertical motion -0.6. Before the
  fix, the stone control was exact and the slime row first differed at tick 0
  only in vertical motion: Java retained `3fdfdc9c5810624e`, while magma
  produced `bfb41205c28f5c29`. Java's living slime callback negates the
  pre-collision negative motion after AABB resolution and before gravity/drag.
  The active-ridden path now preserves that pre-sweep value, selects the exact
  block 0.2 below the final feet position from the existing tagged contact
  list, and applies the callback at the measured boundary. Slime's 0.8
  slipperiness is also selected on the following grounded boundary. This adds
  one contact tag, no state, scratch, allocation, extra scan, or idle work.
  The strict matrix grows from 41 to 43 cases and passes twice in 21.77 and
  22.19 seconds at 30,252 KB harness RSS. JDK 8, product, focused mob/player,
  CPU physics/entity-spine, CUDA compile-only entity-spine, and eleven-layout
  all-source AddressSanitizer gates pass. The broad aggregate takes 5:34.91 at
  445,960 KB peak RSS, zero major faults, and zero swap. The stopped-oracle
  scalar guard passes at 5,004 steps/s against the 3,858.9 floor in
  `trace/out/perf_guard_pig_slime_cpu.json`. GPU execution remained untouched.
  Slime sneaking/nonliving variants, mixed web/slime contact, water, other
  callbacks, server correction/packet timing, saddle
  rendering, and persistence remain open. Global effort remains approximately
  16%.
- F-04 now also promotes the low-speed ridden-pig slime walking callback
  through paired six-tick stone/slime traces starting 0.01 blocks above the
  surface, airborne, with vertical motion -0.05. With bounce already exact,
  the stone control passed and the slime row first differed at tick 1 only in
  horizontal motion: Java Z was `3f7cb9da7117f2aa`, while magma retained
  `3f91b0be16e1b080`; vertical motion matched at `bfa933c36e171b09`.
  The same landing helper now applies Java's
  `0.4 + abs(post-onLanded motionY) * 0.2` X/Z multiplier when the final body
  is grounded and vertical speed is below 0.1, before generic contacts,
  gravity, and friction. It adds only arithmetic after the already-selected
  slime contact, with no new state, scratch, scan, allocation, or idle work.
  The strict matrix grows from 43 to 45 cases and passes twice in 22.61 and
  23.01 seconds at no more than 30,252 KB harness RSS. JDK 8, product, focused
  mob/player, CPU physics/entity-spine, CUDA compile-only entity-spine, and
  thirteen-layout all-source AddressSanitizer gates pass. The broad aggregate
  takes 5:36.20 at 446,292 KB peak RSS, zero major faults, and zero swap. The
  stopped-oracle scalar guard passes at 4,869 steps/s against the 3,858.9 floor
  in `trace/out/perf_guard_pig_slime_walk_cpu.json`. GPU execution remained
  untouched. Slime sneaking/nonliving and mixed-contact variants, water, other
  callbacks, server correction/packet timing, saddle rendering, and
  persistence remain open. Global effort remains approximately 16%.
- F-04 now also promotes bounded client-controlled ridden-pig travel while
  continuously immersed in still source water. The six-tick trace exposes
  `isInWater` and compares raw motion, position, the retained AABB, fall and
  collision state, passenger pose, pig pose/limbs, boost, and private RNG.
  Before the fix, tick 0 used dry travel: Java motion Y/Z were
  `bf947ae147ae147b` / `3f90624dd0e56040`, while magma produced
  `bfb41205c28f5c29` / `3f9f7318c2ca56c0` and reported no water contact.
  The active mounted path now reuses the exact entity-level material/current
  probe, then applies the 1.11.2 water acceleration, AABB move/callbacks,
  0.8 drag, 0.02 gravity, and horizontal edge-climb ordering. A dry control
  exposed an older coordinate-dependent one-ULP AABB reconstruction error;
  ridden pigs now retain the exact swept box between ticks.
  The strict matrix grows from 45 to 46 cases and passes twice in 24.80 and
  24.68 seconds at 30,252 KB harness RSS. JDK 8, product, focused mob/player,
  CPU physics/entity/player, CUDA compile-only entity/player, and fourteen-
  layout all-source AddressSanitizer gates pass. The broad aggregate takes
  5:38.36 at 446,104 KB peak RSS, zero major faults, and zero swap. The
  stopped-oracle scalar guard passes at 4,753 steps/s against the 3,858.9
  floor in `trace/out/perf_guard_pig_water_cpu.json`. The probe is bounded to
  the active ridden pig and adds no allocation, global scan, ordinary-mob, or
  idle work. GPU execution remained untouched. Flowing current, a
  discriminating edge-climb fixture, lava, particles and
  sounds, swim AI, and broader entities remain open. Global effort remains
  approximately 16%.
- F-04 now also promotes the first dry-to-still-water ridden-pig entry
  boundary. A four-tick trace starts 0.05 blocks short of one still source.
  Java completes dry travel on tick 0, then `EntityLivingBase.updateFallState`
  re-runs `handleWaterMovement` from inside `move`: position and motion retain
  the dry values while `isInWater` becomes true, fall distance resets, and
  `Entity.resetHeight` consumes 97 pig-private random draws for splash sound,
  bubbles, and splash particles. Before the fix, magma kept
  `is_in_water=false` and its cursor ended at 27,500,032,739,863 instead of
  Java's 75,376,161,696,822. The bounded active-ridden path now performs the
  post-move material probe and reproduces the width-dependent RNG footprint;
  particle and sound event output itself remains open.
  The strict matrix grows from 46 to 47 cases and passes twice; the timed run
  takes 23.65 seconds at 30,252 KB harness RSS. JDK 8, product, focused mob,
  and fifteen-layout all-source AddressSanitizer gates pass. The broad native
  aggregate takes 5:35.01 at 446,124 KB peak RSS, zero major faults, and zero
  swap. The stopped-oracle scalar guard passes at 4,870 steps/s against the
  3,858.9 floor in `trace/out/perf_guard_pig_water_entry_cpu.json`. The extra
  probe is allocation-free and bounded to an active steerable ridden pig;
  ordinary mobs and idle paths are unchanged. GPU execution remained
  untouched. Broader current gradients and falling-water metadata, rendered
  splash/sound events, lava, swim AI, and broader
  entities remain open. Global effort remains approximately 16%.
- F-04 now also promotes current ordering and falling fall-distance semantics
  on the dry-to-water boundary. The flowing row enters a level-0 source whose
  adjacent level-1 cell produces a pure +X current. Before the fix, tick-0
  Java/native motion X were `3f7f4f50dd2f1aa0` and `3f8cac083126e979`:
  native added the 0.014 current after land drag, while Java applies it inside
  `EntityLivingBase.updateFallState` before callbacks, gravity, and drag. The
  active steerable dry-pig path now splits the exact AABB move from fall-state
  bookkeeping, probes water at the Java interposition point, and then resumes
  landing/block callbacks and the land travel tail. It remains private to the
  host product path; shared CPU/CUDA headers and ordinary mobs are unchanged.
  A separate falling row starts dry at Y=221 with motion Y=-1.0, enters the
  source during the move, consumes the splash RNG footprint, and matches
  fall-distance bits `3f7ae148`: water resets accumulated fall distance first,
  then `Entity.updateFallState` adds the resolved 0.98-block descent.
  The strict matrix grows from 47 to 49 cases and passes twice; the corrected
  timed run takes 26.18 seconds at 30,252 KB harness RSS. JDK 8, product,
  focused mob, and corrected seventeen-layout all-source AddressSanitizer
  gates pass. The broad native aggregate takes 5:38.70 at 446,100 KB peak RSS,
  zero major faults, and zero swap. The stopped-oracle scalar guard passes at
  4,927 steps/s against the 3,858.9 floor in
  `trace/out/perf_guard_pig_water_flow_entry_cpu.json`. The helper uses the
  existing fixed scratch and adds no allocation, global scan, idle work, or
  ordinary-mob work. GPU execution remained untouched. Broader current
  gradients, falling-water metadata/downward currents, edge-climb fixtures,
  emitted splash/sound events, lava, swim AI, and broader entities remain
  open. Global effort remains approximately 16%.
- F-04 now also promotes the ridden-water horizontal-collision climb branch.
  Two one-tick layouts use the same wall and initial motion. The positive row
  leaves the destination dry and matches Java's exact upward replacement
  motion `3fd3333340000000` (`0.30000001192092896`); the blocked control puts
  water in the destination probe and retains the non-climb result
  `3fdcd35a8d0e5603`. Both sides report horizontal collision. The first
  grounded version was rejected because step-height retry avoided the wall;
  the promoted layouts begin off ground and therefore exercise the intended
  branch rather than merely matching its output by accident.
  The strict matrix grows from 49 to 51 cases and passes twice in 28.34 and
  27.59 seconds at 47,520 and 30,252 KB harness RSS. JDK 8, product, focused
  mob, and nineteen-layout all-source AddressSanitizer gates pass. The final
  full native aggregate passes in 6:43.01 at 675,952 KB peak RSS, three major
  faults, and zero swap; its complete survival route also passes from fresh
  spawn through credits. The stopped-oracle scalar guard passes at 5,076
  steps/s against the 3,858.9 floor in
  `trace/out/perf_guard_pig_water_edge_cpu.json`. GPU execution remained
  untouched.
  The broad gate also exposed a pre-existing exact-item regression: Java's
  first process-global entity ID is valid zero, while native rejected it
  after consuming the associated cursors. Native now rejects only negative
  IDs, and a narrow composition test locks exact ID zero. Standalone runtime
  test link lists now include the NBT blob object, and invalid dry-reed,
  unsupported-pressure-plate, held-only combat, unprotected-crystal, and
  unprotected-bed fixtures were replaced with valid survival setups. Broader
  current gradients, falling-water metadata/downward currents, emitted
  splash/sound events, lava, swim AI, and broader entities remain open.
  Global effort remains approximately 16%.
- F-04 now also promotes client-authoritative ridden-pig lava travel and the
  corresponding authoritative server contact state. Still-lava travel,
  dry-to-lava entry, positive/blocked horizontal edge climb, and an overlapping
  water/lava control match exact raw movement, AABB, pose, collision, limb,
  passenger, private-RNG, and independent `isInWater`/`isInLava` state. Water
  wins the travel branch while the raw lava predicate remains observable.
  The separate 12-tick server fixture matches dry and sustained lava through
  health, fire, fall distance, hurt/hurt-resistance timers, last damage,
  alive state, pig-private RNG, and global Math RNG. Native keeps server
  fall/contact/RNG state separate from the client vehicle copy, matching the
  integrated game's two authorities without contaminating movement cursors.
  The complete matrix grows from 51 to 58 strict cases and passes twice; the
  timed pass takes 31.79 seconds at 30,252 KB harness RSS. JDK 8, product,
  focused mob, and 26-layout all-source AddressSanitizer gates pass; the
  sanitizer run takes 40.59 seconds at 96,424 KB. The broad native aggregate
  passes in 6:40.42 at 675,992 KB peak RSS, including the fresh-spawn-through-
  credits route. The stopped-oracle scalar guard remains 5,076 steps/s against
  the 3,858.9 floor in `trace/out/perf_guard_pig_lava_cpu.json`. GPU execution
  remained untouched. Flowing-lava metadata/current controls, packet-stage
  cactus/fire contact, fire resistance and wet extinguish, contact death/drop,
  emitted effects, swim AI, and broader entities remain open. Global effort
  remains approximately 16%.
- F-04 now also promotes the isolated authoritative vehicle-packet cactus and
  fire callback boundary. The Java oracle runs a real
  `NetHandlerPlayServer.processVehicleMove` against a mounted server pig, then
  the matching ordinary pig base tick. Dry, cactus, and fire rows match for 12
  ticks through health, fire, fall distance, hurt and hurt-resistance timers,
  last damage, alive state, pig-private RNG, and process-global Math RNG. The
  unspawned vehicle remains the actual riding entity but is not tracked in the
  isolated world list, preventing unrelated client passenger synchronization
  and extra vehicle packets. Each scheduled server action restores and
  captures its own Math cursor so the integrated client cannot contaminate the
  measured packet sequence.
  Native exposes the same boundary as a cold action immediately before the
  server base tick: the packet's grounded `-1e-6` move clears the authoritative
  fall ledger, cactus damage precedes generic in-fire damage, and fire-counter
  transition/decrement order is exact. It adds no work to the ordinary or idle
  simulation path. The complete strict matrix grows from 58 to 61 cases and
  passes twice in 31.01 and 30.84 seconds at 30,252 KB harness RSS. JDK 8,
  product, focused mob, and 29-layout all-source AddressSanitizer gates pass;
  the sanitizer run takes 43.38 seconds at 96,480 KB. The broad native
  aggregate passes in 6:57.37 at 676,000 KB peak RSS, including the complete
  fresh-spawn-through-credits route. The stopped-oracle scalar guard passes at
  5,008 steps/s against the 3,858.9 floor in
  `trace/out/perf_guard_pig_packet_contact_cpu.json`. GPU execution remained
  untouched. Automatic runtime packet contact integration, combined
  cactus/fire ordering, wet extinguish, fire resistance, packet lava,
  contact death/drop, emitted effects, and broader entities remain open.
  Global effort remains approximately 16%.
- F-04 now also promotes the combined cactus-plus-fire ordering discriminator
  and packet-stage wet burning cleanup. The valid combined layout places
  cactus and supported fire side by side with the pig AABB spanning both.
  Cactus is the sole accepted tick-zero hit and RNG consumer; generic in-fire
  damage is rejected by the newly established hurt resistance, but the fire
  counter still enters its burning state. An earlier fire-above-cactus layout
  was rejected because it did not preserve a valid real-game overlap.
  The separate wet row establishes actual still-water state before setting a
  100-tick burning precondition. The packet move consumes exactly two server
  pig floats for the extinguish sound, then the ordinary base-tick water
  handler reports wet, clears fall distance, normalizes fire to zero, and
  prevents an ON_FIRE hit. Native now retains the authoritative server water
  predicate and performs the same water-before-fire-before-lava base order.
  Its added liquid probe is bounded to the actively ridden server pig, reuses
  the fixed chunk window, allocates nothing, and adds no ordinary-mob or idle
  work.
  The complete strict matrix grows from 61 to 63 cases and passes twice in
  33.21 and 34.00 seconds at 30,252 KB harness RSS. JDK 8, product, focused
  mob, and 31-layout all-source AddressSanitizer gates pass; the sanitizer run
  takes 45.11 seconds at 95,084 KB. The broad native aggregate passes in
  6:55.31 at 675,992 KB peak RSS, including the complete
  fresh-spawn-through-credits route. The stopped-oracle scalar guard passes at
  5,141 steps/s against the 3,858.9 floor in
  `trace/out/perf_guard_pig_packet_wet_cpu.json`. GPU execution remained
  untouched. Automatic runtime packet contact dispatch, fire resistance,
  packet lava, contact death/drop, emitted effects, and broader entities
  remain open. Global effort remains approximately 16%.
- F-04 now also promotes Fire Resistance at the represented ridden-pig server
  contact boundary. Java applies a real hidden-particle potion effect and
  exposes its remaining duration after each ordinary pig update. A two-tick
  packet-fire effect blocks both IN_FIRE and same-tick ON_FIRE without health,
  hurt-state, private-RNG, or Math-RNG changes; after expiry, the next packet
  accepts the ordinary one-point hit. A one-tick lava effect blocks both fire
  sources while preserving the 300-tick ignition and fall-distance halving,
  and the next lava tick resumes normal damage. A combined cactus/fire control
  proves that active resistance does not suppress cactus damage.
  Native reuses the shared exact fire-resistance combat predicate and keeps a
  server-only duration on the represented mounted pig. It checks the effect
  only inside already-active packet/fire/lava branches, decrements it after
  that tick's fire/lava phase, and adds no ordinary-mob scan or allocation.
  The complete strict matrix grows from 63 to 66 cases and passes twice in
  33.26 and 33.96 seconds at 30,252 KB harness RSS. JDK 8, product, focused
  mob, and 34-layout all-source AddressSanitizer gates pass; the sanitizer run
  takes 47.81 seconds at 95,068 KB. The broad native aggregate passes in
  6:51.39 at 675,956 KB peak RSS, including the complete
  fresh-spawn-through-credits route. The stopped-oracle scalar guard passes at
  5,126 steps/s against the 3,858.9 floor in
  `trace/out/perf_guard_pig_fire_resistance_cpu.json`. GPU execution remained
  untouched. Automatic runtime packet contact dispatch, general mob
  potion/effect storage, contact death/drop, emitted effects, and
  broader entities remain open. Global effort remains approximately 16%.
- F-04 now also promotes the packet-stage lava sequence. The Java packet trace
  now records a state immediately after the real vehicle move and another
  after the ordinary server pig update, preventing the final lava state from
  hiding a missing packet callback. Lava is flammable to `Entity.move`, so the
  first packet snapshot shows IN_FIRE health 9, fire 160, last damage 1, fresh
  hurt state and RNG, and the grounded packet's zero server fall distance. The
  following base phase rejects ON_FIRE under hurt resistance, applies the
  larger LAVA differential, and ends at health 6, fire 300, last damage 4,
  timers 9/19, and fall distance zero. A Fire Resistance expiry row proves the
  same packet ignition and lava fire/fall effects without damage while active,
  followed by ordinary packet damage after expiry.
  Native treats fire and lava as the same generic packet flammable-contact
  input before its existing base-tick lava probe. No persistent state, scan,
  allocation, or ordinary runtime work was added. All existing packet rows now
  compare the strengthened packet/post-state pair. The strict matrix grows
  from 66 to 68 cases and passes twice in 32.13 and 32.35 seconds at 30,252 KB
  harness RSS. Product, focused mob, and 36-layout all-source AddressSanitizer
  gates pass; the sanitizer run takes 46.21 seconds at 95,072 KB. The
  immediately preceding unchanged-product broad aggregate passes in 6:51.39
  at 675,956 KB peak RSS, including the complete fresh-spawn-through-credits
  route. The stopped-oracle scalar guard passes at 5,088 steps/s against the
  3,858.9 floor in `trace/out/perf_guard_pig_packet_lava_cpu.json`. GPU
  execution remained untouched. Automatic runtime packet contact dispatch,
  general mob potion/effect storage, contact death/drop, emitted effects, and
  broader entities remain open. Global effort remains approximately 16%.
- F-04 now also promotes automatic represented shared-pose vehicle-packet
  dispatch through the playable runtime. After active carrot-stick pig travel,
  the client side queues the mounted EID; the server consumes it immediately
  after the ordinary player packet and before player timers, hazards, or the
  controlled-living update. The dispatcher derives cactus, flammable, and wet
  contact from the mounted pig's exact world AABB, then reuses the already
  verified packet-contact transition. It allocates nothing, avoids global
  entity scans, and enters the bounded block walk only when a packet is
  pending.
  A runtime-only native fixture primes the public `gm_runtime_tick` path and
  never calls the cold packet helper. Nine runtime rows duplicate the dry,
  cactus, fire, combined, wet, lava, and resistance boundaries while checking
  an automatic packet checkpoint, raw pose/AABB, all contact state, and RNG
  cursors. Exposing pose found and fixed the earliest fixture contamination:
  a real server `EntityPlayerMP` passenger cannot steer, while the old native
  server fixture had silently advanced the pig by 0.05625 blocks on tick zero.
  The measured authoritative rows now empty the carrot stick before their base
  tick and remain stationary on both engines.
  The complete 77-case Java/native matrix passes twice in 36.15 and 36.29
  seconds at 30,252 KB harness RSS. The all-source AddressSanitizer build runs
  the same 77 cases in 139.48 seconds at 100,880 KB. JDK 8, the CPU product,
  the focused mob gate, and the broad fresh-spawn-through-credits aggregate
  pass; the broad gate takes 488.59 seconds at 676,552 KB peak RSS. With the
  oracle stopped, scalar throughput is 5,061 steps/s against the 4,062
  baseline and 3,858.9 floor in
  `trace/out/perf_guard_pig_runtime_packet_cpu.json`. GPU 1 was untouched.
  This promotion carries identity against the runtime's represented shared
  accepted pose. Arbitrary moving vehicle deltas, collision rollback,
  wrong-move/speed validation, correction packets, and independently retained
  client/server vehicle poses remain open. Global effort remains approximately
  16%.
- F-04 now also promotes a bounded dry authoritative moving-vehicle packet
  transition. The oracle invokes the real
  `NetHandlerPlayServer.processVehicleMove` and snapshots both hidden tracker
  triplets immediately after the packet. Native matches an accepted horizontal
  move, rollback from a two-high stone wall, and the greater-than-100 speed
  rejection through raw position, AABB, motion, rotation, on-ground and fall
  state, tracker advancement, rollback branch, ordinary post-packet pig update,
  and every already-covered health, contact, and RNG field. Exact filtering of
  the block broadphase fixed the earliest wall mismatch instead of treating its
  below-floor candidate cells as collisions.
  The complete 80-case Java/native matrix passes twice in 38.62 and 38.60
  seconds at 30,252 KB harness RSS. Its all-source AddressSanitizer and
  LeakSanitizer run passes all 80 cases in 151.36 seconds at 100,900 KB. JDK 8,
  the CPU product, focused mob gate, and broad fresh-spawn-through-credits game
  aggregate pass. The broad gate took 633.66 seconds at 676,164 KB while the
  Java oracle concurrently occupied about eight CPU cores. With the oracle
  stopped, scalar throughput is 5,140 steps/s against the 4,062 baseline and
  3,858.9 floor in
  `trace/out/perf_guard_pig_vehicle_move_cpu.json`. GPU 1 was untouched.
  This direct helper deliberately owns one dry server body and supports only
  same-height horizontal packets. Runtime retention of independent client and
  server poses, moving contact callbacks, vertical deltas, outbound correction
  delivery, and persistent tracker state remain open. Global effort remains
  approximately 16%.
- F-04 now also promotes the dry vehicle-packet boundary through the public
  runtime with a full client target payload and an independently retained
  authoritative pig body. One row obtains the exact nonzero target from a real
  client `gm_runtime_tick`, restores the pre-emission server snapshot while
  retaining that queued payload, and consumes it on the following runtime
  tick. Two explicitly labeled injected rows exercise wall and speed
  corrections that a normal client would not emit. Native keeps client pose
  and AABB bit-identical at the immediate packet checkpoint while the server
  shadow accepts or corrects the target and advances its packet sequence.
  This is a compositional proof: native emission and queue consumption are
  compared at the packet boundary with the real Java
  `NetHandlerPlayServer.processVehicleMove` result. It is not claimed as a
  Java integrated-client-to-server end-to-end trace.
  Acceptance review found that Java block callbacks observe `Entity.move`'s
  resolved server AABB before handler rollback, while water state follows
  `EntityLivingBase.updateFallState`'s asymmetric refresh. The existing
  callback helper observes the shared stationary AABB,
  so runtime dispatch now invokes it only for exact zero-delta packets instead
  of incorrectly generalizing it to moving targets. Resolved moving contacts,
  authoritative server base-tick advancement, vertical and multiple packets,
  outbound correction delivery, and client correction application remain
  open.
  The complete 83-case Java/native matrix passes twice in 44.17 and 43.22
  seconds, with steady-state harness RSS of 30,252 KB. A clean-oracle
  all-source AddressSanitizer and LeakSanitizer run passes all 83 cases in
  159.34 seconds at 102,288 KB. JDK 8, product, focused mob, and broad game
  gates pass; the final broad fresh-spawn-through-credits aggregate takes
  417.74 seconds at 675,980 KB. With the oracle stopped, scalar throughput is
  5,132 steps/s against the 4,062 baseline and 3,858.9 floor in
  `trace/out/perf_guard_pig_runtime_vehicle_move_cpu.json`. GPU 1 was
  untouched. The global effort estimate remains approximately 16%.
- F-04 now also promotes the bounded dry authoritative pig base state after
  the runtime vehicle-packet handler. The represented server body advances
  independently from the client EwStore: its own AABB supplies liquid probes,
  server event coordinates follow that body, and a steering-item
  `EntityPlayerMP` passenger aligns server yaw/pitch while the non-local server
  branch zeros XYZ motion. Shared health/timers and the server-private entity
  RNG remain in their existing Java order. The zero-delta packet path now also
  clears the independent server fall ledger at the packet move boundary.
  The three runtime cases retain the carrot-on-a-stick through the base tick
  and naturally queue the next client packet. They compare the immutable
  immediate packet checkpoint and a newly exposed post-base server state
  against Java through raw pose/AABB, motion, rotation, on-ground/fall state,
  both tracker triplets, liquid flags, health/fire/hurt/effect state, and both
  RNG cursors. The older direct one-body helper is explicitly isolated from
  the runtime's dual-body shadow so it remains a separate packet-algorithm
  proof.
  The complete 83-case matrix passes twice in 43.64 and 44.49 seconds at
  30,252 KB harness RSS. A clean-oracle all-source AddressSanitizer,
  UndefinedBehaviorSanitizer, and LeakSanitizer run passes all 83 cases in
  176.78 seconds at 124,084 KB. JDK 8, product, focused mob, and broad game
  gates pass; the broad fresh-spawn-through-credits aggregate takes 414.01
  seconds at 676,440 KB, with one major fault and zero swap. With the oracle
  stopped, scalar throughput is 5,182 steps/s against the 4,062 baseline and
  3,858.9 floor in `trace/out/perf_guard_pig_server_base_cpu.json`. GPU 1 was
  untouched. This promotion is dry and one-packet: divergent dual-pose
  water/lava base ticks, complete previous/render/limb bookkeeping,
  later-update water-entry RNG, vertical and same-epoch multiple packets, correction
  delivery, and client correction application remain open. Global effort
  remains approximately 16%.
- F-04 now also promotes resolved moving contacts for the bounded first
  runtime vehicle packet. Cactus callbacks and the generic fire/lava check run
  at `Entity.move`'s collision-resolved temporary authoritative AABB before
  the handler accepts or restores XYZ. Accepted fire/lava, corrected cactus,
  and wall-beyond-fire/cactus controls prove both positive callback paths and
  reject scans at the raw target. Contact damage, ignition, hurt timers, RNG,
  motion/fall state, and tracker effects survive correction exactly as Java
  requires.
  Water follows the less obvious `EntityLivingBase.updateFallState` rule. A
  previously dry pig probes water during the packet move and immediately
  enters it, while a previously wet pig moved into fire retains stale wetness
  until the following base tick. The first-packet entry deliberately consumes
  no splash RNG because the freshly constructed Java pig still has
  `firstUpdate=true`; later-update resetHeight RNG remains the next two-packet
  boundary. Native factors the contact scan/transition/checkpoint around the
  temporary server AABB and removes the old external zero-delta-only runtime
  dispatch. It reuses fixed scratch and performs no allocation or idle scan.
  The complete 90-case matrix passes twice in 47.24 and 47.76 seconds at
  30,252 KB RSS. The all-source ASan/UBSan/LSan build passes the same matrix in
  201.10 seconds at 124,100 KB. The first sanitizer attempt hit the documented
  Java packet-lava fixture contamination without a sanitizer diagnostic; the
  exact row and complete repeat passed. JDK 8, product, focused mob, and broad
  game gates pass; the broad fresh-spawn-through-credits aggregate takes
  415.64 seconds at 676,204 KB with zero major faults and zero swap. With the
  oracle stopped, scalar throughput is 4,951 steps/s against the 4,062
  baseline and 3,858.9 floor in
  `trace/out/perf_guard_pig_moving_contact_cpu.json`. GPU 1 was untouched.
  Global effort remains approximately 16%.
- F-04 now also promotes bounded persistent vehicle-packet tracker epochs and
  later water-entry RNG. Two accepted packets in one handler epoch retain the
  original `lowestRidden` speed baseline while only `lowestRidden1` advances;
  the following server tick re-seeds both triplets from the authoritative pig
  pose. A 10.1-block absolute second target distinguishes that re-seed from a
  stale-origin false speed correction. The server vehicle shadow retains
  `Entity.firstUpdate` from construction state rather than re-arming it on
  mount. Both a mounted first-base transition and a pre-ticked saved-state
  mount then enter water through the packet mover with `inWater=true`, zero
  fall/fire state, and the exact 97 `Entity.rand.nextFloat()` transitions for
  an adult pig; fresh first-update entry remains the established zero-draw
  control. All paths use fixed state and the existing local collision window.
  The final 94-case Java/native matrix passes in 53.98 seconds at 30,252 KB
  harness RSS after the preceding 93-case matrix passed twice in 49.14 and
  49.07 seconds. A clean-oracle all-source ASan/UBSan/LSan run passes all 94
  cases in 201.61 seconds at 119,344 KB. JDK 8, the CPU product, focused mob,
  and broad fresh-spawn-through-credits gates pass; the broad aggregate takes
  408.88 seconds at 675,984 KB with zero major faults and zero swap.
  Stopped-oracle scalar throughput is 5,049 steps/s against the 4,062 baseline
  and 3,858.9 floor in
  `trace/out/perf_guard_pig_packet_chain_cpu.json`. GPU 1 was untouched.
  Vertical packets, higher-count/mixed-result bursts, correction delivery and
  client application, and complete server previous/render/limb bookkeeping
  remain open. Global effort remains approximately 16%.
- F-04 now also promotes bounded vertical ridden-pig vehicle packets through
  both the direct authoritative seam and the integrated dual-pose runtime.
  Open upward movement is accepted at the exact target Y, downward movement
  whose contracted target intersects the floor is corrected, and a ceiling
  target is corrected after retaining the temporary `Entity.move` side
  effects. The residual check deliberately remains horizontal-only, matching
  Java 1.11.2's always-true finite-Y residual reset. Two accepted upward
  packets in one network epoch additionally prove that `lowestRiddenY1`
  advances while the primary speed origin remains fixed. The 101-case matrix
  passes twice in 54.84 and 54.75 seconds at 30,252 KB; the all-source
  ASan/UBSan/LSan matrix passes in 250.99 seconds at 124,340 KB. JDK 8, product,
  focused mob, and broad fresh-spawn-through-credits gates pass; the broad
  aggregate takes 429.19 seconds at 675,996 KB with zero major faults and zero
  swap. Stopped-oracle scalar throughput is 4,961 steps/s against the 4,062
  baseline and 3,858.9 floor in
  `trace/out/perf_guard_pig_vertical_packet_cpu.json`. GPU 1 was untouched.
  Higher-count/mixed-result and long mixed-axis packet bursts, correction
  delivery/application, and complete server previous/render/limb bookkeeping
  remain open. Global effort remains approximately 16%.
- F-04 now also promotes one higher-count mixed-result vehicle epoch and the
  bounded correction delivery/application path. Four packets without a server
  base step produce accept, collision rollback, speed rollback, then accept;
  the exact Java tracker origin and advancing secondary triplet match after
  every packet. A real `NetHandlerPlayClient.handleMoveVehicle` fixture proves
  that a correction rewrites only the mounted pig pose/AABB, preserves motion
  and on-ground state, leaves the passenger position unchanged at the callback,
  and emits an immediate exact-pose `CPacketVehicleMove` acknowledgement.
  Runtime carries that acknowledgement separately from the later predicted
  vehicle packet in a fixed two-entry FIFO and drains both in the next server
  epoch without allocation. The 104-case matrix passes twice in 55.45 and
  55.83 seconds at 30,252 KB; a discarded long-session dry-contact fixture
  failure passed immediately in isolation and after a clean oracle restart.
  The all-source ASan/UBSan/LSan matrix passes in 242.12 seconds at 123,988 KB.
  JDK 8, product, focused mob, and broad fresh-spawn-through-credits gates
  pass; the broad aggregate takes 431.72 seconds at 675,992 KB with zero major
  faults and zero swap. Stopped-oracle scalar throughput is 4,909 steps/s
  against the 4,062 baseline and 3,858.9 floor in
  `trace/out/perf_guard_pig_correction_delivery_cpu.json`. GPU 1 was untouched.
  Long mixed-axis and repeated-correction bursts, broader network scheduling,
  and complete server previous/render/limb bookkeeping remain open. Global
  effort remains approximately 16%.
- O-02 now has a checksummed version-1 neutral capsule, deterministic event
  ordering, player/inventory/block/dimension/time/air restore hooks on both
  sides, and round-trip plus malformed/checksum-negative controls. Living
  NoAI pigs restore their exact entity ID, pose, motion, health, and combat
  timers; XP orbs additionally restore value, age, pickup delay, color, and
  the hidden target-refresh cursor. The melee and XP matrix cases each contain
  one capsule-emitted spawn, with no second fixture injection. Pending updates
  now restore absolute due time, priority, and relative tie-break order for
  inert stone and the proof-safe first water-source dispatch. O-02 remains
  active because general entity payloads, general scheduled work, tile
  entities, per-entity RNGs, and the complete loaded random-tick active set are
  not yet represented. The internal 48-bit `World.rand` and signed 32-bit
  `World.updateLCG` cursors now restore exactly. When supplied, the capsule's
  one-byte-per-cell saved-skylight payload is length/range/checksum validated,
  restored after the block batch resolves, and advertised as exact; capsules
  without it continue to advertise that field as captured-only.
  Java's process-global next-entity-ID cursor is captured but intentionally
  not restored: integrated-client construction outside the bounded capsule
  consumes it. Emergent falling blocks instead use origin plus block state as
  their stable lifecycle identity; raw EIDs remain diagnostic.
- W-01 has its first active exact slice. A level-0 dynamic-water source on a
  stone floor remains pending for ticks 0-1, dispatches on tick 2, creates four
  level-1 neighbors, and leaves five child updates due five ticks later in
  Java's NORTH/source/SOUTH/WEST/EAST order. On tick 7 that ordered batch
  settles the source static, creates the level-2 ring, and leaves 12 exact
  descendants. A raised source separately creates metadata-8 falling water,
  requeues the falling cell before its woken source, then creates exact
  level-1 rings in both layers on tick 7. Both 10,625-cell post-states are
  byte-exact and require 13 and 9 mutations respectively. More complex water
  materials/shapes remain queued within W-01.
- W-01 lava now covers the natural 30-tick Overworld cadence and two
  deterministic flat-plane generations. The source dispatches on tick 27,
  creates four level-2 neighbors, and leaves five children due 30 ticks later;
  their tick-57 batch settles the source, creates the level-4 ring, and leaves
  12 ordered descendants. A separate enclosed-water case drains its water
  update on tick 2, turns the water source into stone when raised lava flows
  down on tick 27, and requeues the woken lava source at +30. Random lava
  rescheduling, other reactions, general loaded-world random-tick iteration,
  broader fire materials/weather, and broader lighting shapes remain queued.
- W-01 falling blocks now cover metadata-0 sand and gravel over a two-cell
  clear air column and stone support. Their scheduled update remains pending
  on tick 0, dispatches on tick 1, removes the source, and advances a transient
  `EntityFallingBlock` with the exact block identity in that same tick. Exact
  position, velocity, origin, and fall time match through tick 9; tick 10
  lands at y=78 and schedules the vanilla +2 stability update, which drains on
  tick 12. Each material's two raw mutations and all 10,625 final cells match.
  A one-cell replaceable landing cell is also exact for still water, static
  lava, and fire, including raw block light. A top-half stone slab presents a
  full-height surface, so sand lands in the air cell above it and follows the
  same `+2` callback lifecycle; a bottom-half slab instead follows the exact
  failed-placement item-drop path. Grass path adds the metadata-zero 15/16
  failed-placement surface and exact tick-10 item branch; soul sand adds the
  7/8 surface and tick-11 branch; an enchanting table adds the 3/4 surface,
  tick-11 branch, and partial-surface EntityItem ascent; supported carpet adds
  the 1/16 surface, tick-13 branch, and the same exact ascent rule. Snow layers
  cover metadata-dependent collision from zero through 7/8 height, including
  one-layer replacement and the seven nonreplaceable item paths. Farmland adds
  the moisture-invariant 15/16 path and whole cake adds a centered 1/2 path.
  Gravel flint RNG,
  `doEntityDrops=false`, dynamic fluid columns and
  flow, fire callbacks, anvils, lateral motion, and other collision shapes
  remain queued.
- W-01 random-tick callback parity now covers wheat. The capsule captures and
  restores the exact internal 48-bit `java.util.Random` seed. A controlled
  server-thread call invokes vanilla `Block.randomTick`; magma matches its
  light gate, fertile-soil weighting, crop-layout penalty, float truncation,
  `nextInt(7)`, and the sole age-0-to-age-1 mutation.
- W-01 fire now covers the dry, non-humid NORMAL proof region containing only
  air, stone, planks, and fire. The Java oracle records humidity, difficulty,
  `doFireTick`, and rain for pending fire entries. `Random(seed=36)` consumes
  the exact 11-draw callback sequence, turns only the east plank into age-zero
  fire, and leaves the exact two-entry pending queue. A separate fixture
  replaces the placement-created update with a +3 callback, reseeds immediately
  before its due world tick, proves the tick-2 dispatch, and matches both +35
  descendants. A disabled-gamerule counterpart preserves the pending queue
  through observations 0-1, drains it at observation 2, consumes no callback
  RNG, and makes no block or successor-queue change. An isolated age-four fire
  on stone now proves the two-draw reschedule-before-burnout branch. Age-15
  fire on Overworld or Nether netherrack and End bedrock proves every admitted
  infinite-source dimension/source pair, persistent source queues, and the
  exact seven-draw no-spread callback. Steady Overworld rain now restores its exact
  timers plus the five authoritative `isRainingAt` probes for isolated age-15
  fire on stone. Both source-exposed and source-covered/cardinal-neighbor-
  exposed `canDie` topologies are exact. `Random(1024)` takes the one-float
  early extinguish path with no successor; `Random(0)` fails that threshold,
  consumes the schedule draw, leaves the exact +38 stale successor, then burns
  the source out. Scheduled oracle callbacks restore and record World.rand at
  the real `BlockFire.updateTick` boundary, eliminating server-queue RNG
  contamination while proving the exact before/after cursor. All six
  scheduled/direct rain branches pass three independent times at
  `trace/out/matrix_fire_rain_exact_hook_repeat_3/summary.md`. Effective thunder
  now restores through the same capsule and follows vanilla's identical
  one-float extinguish branch; its scheduled/direct rows pass 6/6 at
  `trace/out/matrix_fire_thunder_age15_repeat_3/summary.md`. Nether netherrack
  now also survives the same exact seven-draw callback in dimension -1; three
  repeats pass over 4,913 raw cells at
  `trace/out/matrix_fire_nether_netherrack_repeat_3/summary.md`. All 16 affected
  fire branches pass together at
  `trace/out/matrix_fire_weather_nether_family/summary.md`. Fire can now ignite
  adjacent TNT through vanilla's post-replacement EXPLODE hook. The controlled
  callback matches source age 0-to-1, TNT-to-air, nine World RNG draws, two
  Math RNG draws, one entity-ID allocation, the exact primed-TNT motion/fuse,
  and the unchanged source queue. Three repeats pass at
  `trace/out/matrix_fire_tnt_repeat_3/summary.md`, and all 17 affected rows pass
  at `trace/out/matrix_fire_weather_tnt_family/summary.md`. Final native
  coverage passes in 5:28.44 at 315,368 KB with zero swap. The last clean
  scalar guard remains 4,187 steps/s against the unchanged 3,858.9 floor;
  recaptures under the current unrelated 64-thread host load are retained as
  contaminated diagnostics, not promoted. The source-humidity branch is also
  exact: `Random(0)` distinguishes the direct 300/250 denominator, while
  `Random(776)` distinguishes the normal threshold two from the humidity-
  halved threshold one at the first volumetric candidate. Both pairs pass
  three repeats, and all 21 fire/weather controls pass at
  `trace/out/matrix_fire_weather_humidity_spread_family/summary.md`. The
  post-assertion runtime aggregate passes in 5:53.34 at 253,264 KB with zero
  major faults. Rain-aware direct target burning is exact too. A
  precipitation-transparent east tall-grass target and its roofed control
  isolate `tryCatchFire` after the same successful chance/fate rolls. The
  exposed target burns to air without age/child draws; the covered target
  becomes age-zero fire and queues its +35 child. Both pass three repeats, all
  23 affected fire/weather rows pass at
  `trace/out/matrix_fire_weather_rain_target_family/summary.md`, and the native
  runtime aggregate passes in 5:32.01 at 253,108 KB. The first volumetric
  candidate now has the same location-sensitive rain guard. `Random(125)`
  reaches roll zero against threshold three in both paired fixtures; the
  exposed candidate is suppressed at cursor `0x06F23450DB83`, while its
  five-roof covered twin becomes age-zero fire, queues +35, and ends at
  `0xE9AD9F0B0D75`. All six repeats pass at
  `trace/out/matrix_fire_rain_volume_repeat_3_final/summary.md`, all 25
  affected fire/weather rows pass at
  `trace/out/matrix_fire_weather_rain_volume_family_final/summary.md`, and the
  native aggregate passes in 5:40.24 at 252,144 KB with zero major faults.
  The first non-plank direct materials are exact as well. Wool uses the
  vanilla 30/60 table row, changes only to age-zero fire, retains two +35
  callbacks, and ends at the same eleven-draw cursor `0x8EBD372F3662` as the
  plank topology. Oak logs use the 5/5 row; `Random(57)` creates only the east
  age-zero fire, queues source/child at +31/+38, and ends at
  `0x27DB2C1FBC09`. Repeats and affected families pass at
  `trace/out/matrix_fire_wool_repeat_3/summary.md` and
  `trace/out/matrix_fire_weather_log_family/summary.md`. Dry tall grass adds
  the 60/100 row: `Random(4)` ages the source zero-to-one, burns east 31:1 to
  air without a child, and ends its exact nine-draw World cursor at
  `0x1411389CAF08`. Three repeats pass and all 30 affected fire/weather rows
  pass at
  `trace/out/matrix_fire_weather_log_tallgrass_family/summary.md`. Bookshelf
  corrects the one discovered table defect: id 47 now uses Java's 30/20 row in
  both runtime and shared CPU/CUDA code. The direct flammability row and an
  isolated volumetric roll-two threshold discriminator pass 6/6 repeats, and
  all 32 affected rows pass at
  `trace/out/matrix_fire_bookshelf_table_family/summary.md`. The shared table's
  CPU assertion passes at
  `trace/out/test_world_tick_bookshelf_table_cpu.log`; CUDA execution remains
  deferred with the shared GPU. Hay and carpet now have explicit 60/20
  host/device assertions, while direct and volumetric hay fixtures prove both
  fields with exact `Random(36)` and threshold-discriminating `Random(391)`
  callbacks. The combined 44-case falling/fire family passes at
  `trace/out/matrix_enchanting_table_hay_family/summary.md`, and the 38-line
  CPU table test passes at
  `trace/out/test_world_tick_hay_carpet_table_cpu.log`. Fire
  placement now
  activates both smallest Nether-portal axes, producing six 90:1 or 90:2 cells
  with no queue or cursor draw. Its one-block-broken control retains fire and
  queues +39 after the sole `nextInt(10)=9` draw. The former centered staging
  miss on a legal 2x16 X frame is fixed by floor-qualified bottom-row edge
  alignment; the shared 32-cube and CUDA state shape stay unchanged. Z/tall
  repeats pass 6/6, all 31 affected rows pass at
  `trace/out/matrix_fire_weather_portal_axis_height_family/summary.md`, and
  focused native tests cover both 21x21 orientations plus a broken maximum
  frame. The combined native aggregate passes in 6:17.39 at 331,732 KB.
  The clean scalar guard passes at 4,392 steps/s against the frozen 4,062
  baseline and 3,858.9 floor at
  `trace/out/perf_guard_fire_log_portal_axis_cpu_1.json`. The later combined
  tall-grass/grass-path guard raises the clean scalar median to 5,102 steps/s
  at `trace/out/perf_guard_tallgrass_grass_path_cpu_1.json`. After bookshelf
  and soul sand, the full native aggregate passes in 6:38.12 at 335,628 KB and
  the clean scalar guard passes at 4,853 steps/s at
  `trace/out/perf_guard_soul_sand_bookshelf_cpu_1.json`. The subsequent
  enchanting-table/hay aggregate passes in 6:48.67 at 346,044
  KB with zero major faults or swap, and its clean scalar guard passes at
  4,671 steps/s at
  `trace/out/perf_guard_enchanting_table_hay_cpu_1.json`. Broader rain-aware
  spread/material branches, activation topologies, and an unconstrained
  world's intervening RNG consumers remain open.
- R-01 now has its first exact consumer/producer slice. A tick-0 redstone-block
  placement follows Java's WEST/EAST/DOWN/UP/NORTH/SOUTH notification order
  and powers an adjacent unlit lamp in the same tick. Removing the source
  leaves the lit lamp unchanged for observations 0-2, stores one due
  callback at absolute total-time +4, dispatches on observation 3, and changes
  block 124 back to 123. A separate capsule-loaded powered callback preserves
  its absolute due time, priority, and order, drains on the same tick, and
  correctly leaves the lamp lit. The capsule accepts direct redstone-block
  power or a bounded unpowered air/stone/log/leaves proof region and rejects
  an adjacent unimplemented power producer. All three cases match raw blocks;
  the mutating cases also match every one of 10,625 raw block-light cells.
  General strong/indirect power beyond controls/wire/torch-through-stone, broader
  vertical-dust materials/topologies, and more controls remain active.
  Arbitrary post-observation checkpointing now has an exact torch-history
  continuation proof.
- R-02 now includes the first lever slice. A supported floor lever uses
  metadata 5 when off and 13 when powered. The live 5-to-13 edit provides
  direct weak power and lights its east lamp immediately; 13-to-5 retains the
  lamp for three observations, dispatches its +4 callback on the fourth, and
  turns it off. A capsule-loaded pending lamp recognizes the powered lever and
  drains as a no-op. The light gate caught the legacy cut-ID fallback treating
  lever 69 as opaque: its exact zero opacity now lets light 14 propagate west
  through the lever. All three focused and aggregate cases are exact.
- R-02 stone buttons now have a saved-pulse proof. A powered floor button
  starts as 77:13 with one callback due at total-time +20. It remains pending
  for observations 0-18, releases to 77:5 on observation 19, and creates the
  lamp's independent callback at +24. That queue remains through observation
  22 and turns the lamp off on 23. The capsule promotes only the powered
  floor-button/east-lamp proof region and excludes the released-button
  negative. Button opacity is zero, and the final raw light field matches all
  10,625 cells.
- R-02 now includes the first stone-pressure-plate slice. A powered metadata-1
  plate restored with a callback due at total-time +3 remains queued for
  observations 0-1, releases to metadata 0 on observation 2, and hands its
  adjacent lamp an independent callback due four ticks later. The callback
  uses the exact inset, quarter-block-high living-entity AABB; the runtime
  checks the authoritative player, represented living active set, and End
  dragon only when the callback is due. The capsule promotes only the bounded,
  unoccupied +3 proof region and rejects nearby unrepresented living state.
  All seven queue observations, both raw mutations, and 10,625 block/light
  cells are exact.
- R-02 live plate activation now includes represented living mobs, not only
  the integrated-server player. After the mob pass, a fixed-capacity scan
  visits each living entity's contracted collision cells and invokes the same
  metadata-0 plate transition. The taskless collision-enabled pig activates
  at observation 0 with absolute due time +21, remains byte-exact and
  stationary, and causes the due callback at observation 20 to schedule the
  next +20 check. A true NoAI pig is excluded from this collision pass but
  remains visible to an already-powered plate's MOBS sensitivity query.
- R-02 wooden plates now distinguish EVERYTHING from stone's MOBS
  sensitivity. The Java bridge creates a real stationary `EntityItem` with
  exact EID, stack, pose/motion, age, and pickup delay; magma replays that
  sidecar and compares every field. Active items expose exact 0.25-by-0.25
  AABBs. After their entity tick, runtime visits only their contracted cells,
  activates metadata-0 block 72, and schedules absolute +21/+41 callbacks.
  The item pool is not scanned at all when `n_active == 0`, and an identical
  item over stone is the unit-level non-activation control.
- R-02 weighted plates now implement vanilla's all-entity analog rule:
  `ceil(min(entity_count, max_weight) / max_weight * 15)`, with max weights
  15 for gold block 147 and 150 for iron block 148. Player plus one
  `EntityItem` therefore produces exact plate/dust strengths 2 and 1
  respectively, lights the lamp, and renews the callback at +10. The native
  negative proves one stack of 64 items still counts as one entity, not 64.
  A capsule-restored gold plate additionally proves strength 2-to-0 release,
  immediate dust drain, and the lamp's independent +4 handoff. The capsule
  promotes only a supported imminent callback with no unrepresented entity
  in its conservative proof prism. Plate occupancy uses only the fixed player,
  mob, item, and dragon sets; it performs no loaded-world scan or allocation.
- R-02 now includes boats in the inherited default trigger path. A boat's
  exact 1.375-by-0.5625 AABB reaches all three segments of the focused
  tripwire line, powers both hooks and lamps, and preserves the exact three
  +10 wire callbacks. The real Java-vs-magma gate passes entity state, queue
  order, seven raw mutations, and all 10,625 block/light cells at
  `trace/out/matrix_boat_redstone_6/summary.md`. A separate real-Java negative
  proves the same boat leaves a stone MOBS-sensitive plate unpowered with an
  empty queue and 10,625 unchanged cells at
  `trace/out/matrix_boat_redstone_negative_1/summary.md`. Wooden and weighted
  EVERYTHING plates include boats; stone deliberately does not. Both queries
  reuse the fixed represented-entity store and add no world scan or heap work.
- R-02 now includes XP orbs in that inherited default trigger path. Vanilla's
  0.5-by-0.5 `EntityXPOrb` calls `doBlockCollisions` after moving, so magma
  captures its post-move box during the existing orb pass. A real Java-vs-magma
  case powers the middle tripwire segment, both hooks, and both lamps on tick
  zero with five exact raw mutations. The paired negative proves the same orb
  leaves a stone MOBS-sensitive plate unpowered for two ticks with an empty
  queue and all 10,625 cells unchanged. Both exact-state/raw/light cases pass
  at `trace/out/matrix_xp_redstone_2/summary.md`. Wooden and weighted
  EVERYTHING plates count live orbs; stone deliberately does not. The idle
  path adds only one branch, while active collision boxes reuse the existing
  fixed 64-orb pass with no allocation or loaded-world scan.
- R-02 now includes arrows in the same tripwire and EVERYTHING-plate path.
  Vanilla `EntityArrow` uses a 0.5-by-0.5 box, inherits the default pressure
  trigger rule, and invokes block collisions after moving. The 12-tick
  Java-vs-magma case matches the powered middle wire, both hooks, both lamps,
  exact +10 lifecycle, five raw mutations, and every compared state feature.
  The paired two-tick negative leaves a stone MOBS-sensitive plate unpowered,
  with an empty queue and 10,625 unchanged cells. Both pass at
  `trace/out/matrix_arrow_redstone_2/summary.md`. Wooden and weighted plates
  count the arrow; stone deliberately does not. Collision traversal and
  occupancy reuse the fixed 32-projectile pool and inspect only active-arrow
  cells, without a world scan or allocation.
- R-02 now includes active falling blocks in that inherited trigger path.
  Local `EntityFallingBlock` uses a float-exact 0.98-by-0.98 box and invokes
  `doBlockCollisions` from `move` before drag and landing settlement. In the
  focused case, scheduled sand crosses a suspended attached line and powers
  the middle wire, both hooks, and both lamps on observation 10. Its exact
  ticks 1..11 trajectory, queue order, source removal, and five circuit
  mutations pass beside the unchanged ordinary landing lifecycle at
  `trace/out/matrix_falling_redstone_1/summary.md`. Native controls prove wood
  and gold include the same entity, stone MOBS sensitivity excludes it, and a
  small fireball remains a non-trigger because its vanilla tick never enters
  block collisions. Work remains behind the existing nonzero
  `falling_block_count` branch and scans only the fixed 16-entry store.
- R-02 now distinguishes projectile activation from scheduled occupancy.
  Vanilla `EntityFireball.onUpdate` assigns position directly and never calls
  `doBlockCollisions`, so a small fireball cannot turn on an unpowered wire.
  `BlockTripWire.updateState`, however, queries all `Entity` instances during
  an already-powered wire's callback, and `EntitySmallFireball` inherits the
  default pressure-trigger predicate. The deliberate old-C gate drops the
  wire at its due callback while Java retains it at
  `trace/out/matrix_small_fireball_tripwire_probe_1/summary.md`. The corrected
  powered-hold and unpowered-negative cases pass exact entity state, queue
  timing, and all 10,625 unchanged raw cells at
  `trace/out/matrix_small_fireball_tripwire_1/summary.md`. Occupancy scans the
  existing fixed 32-projectile pool only when a wire or EVERYTHING plate is
  queried; arrow-only button activation remains separate, with no new
  allocation or loaded-world pass.
- R-02 wooden buttons now implement vanilla's arrow-only occupancy path.
  The Java bridge creates an exact gravity-free, zero-motion
  `EntityTippedArrow`; magma restores its authoritative EID and pose in the
  existing fixed 32-projectile pool. A floor-mounted button uses the exact
  pressed/unpressed selection AABB, changes 143:5 to 143:13 on overlap,
  powers its lamp, and replaces its due callback at +30 while the arrow
  remains. Removing the arrow causes the next due callback to release the
  button and hand the lamp its independent +4 callback. The stone-button
  negative remains unpowered under the identical arrow. Capsule promotion is
  limited to an imminent arrow-free callback and rejects every captured
  vanilla arrow subtype. Block 143's registry-backed hardness, opacity, and
  random-tick flags are exact, so the light field is no longer hidden by the
  cut-ID opaque fallback. All arrow/button queries are over the fixed
  projectile pool; no world scan or hot-loop allocation was added.
- R-03 repeaters cover IDs 93/94 with all four horizontal facings and
  metadata-derived delays +2/+4/+6/+8. Rising and falling edges retain Java's
  priorities -1/-2, an output repeater selects priority -3, and a vanished
  input still forces the minimum powered pulse. Perpendicular powered
  repeaters lock the main transition; removing the locker supplies the
  neighbor notification that starts the full delay. Weak and strong outputs
  are directional, including a lamp beyond an ordinary stone output block.
  Capsule restore promotes only bounded registry-supported repeater proof
  regions and preserves saved on, off, and minimum-pulse callback chains.
  The fixed-capacity scheduled queue and edit/callback-driven neighbor path
  add no inactive per-tick scan.
- R-03 comparator core covers IDs 149/150, all horizontal facings, compare
  and subtract modes, analog rear and side strengths, directional weak/strong
  output, and vanilla's two-tick callback priorities. A fixed 64-entry tile
  pool preserves `outputSignal` only for loaded comparators; capsule restore
  validates and restores that tile value plus proof-safe callbacks for both
  powered edges. Threshold fixtures include the non-obvious compare
  rear-7/side-8 state (tile output 7 while unpowered) and subtract
  rear-7/side-7 state (accepted callback with unchanged output zero). A
  comparator-to-repeater chain proves priority -1 propagation. Work remains
  edit/callback driven with no inactive scan or hot-loop allocation.
  The first input-override bundle is now complete for cake, cauldron,
  and End portal frames, including the one-normal-cube look-through rule and
  `World.updateComparatorOutputLevel` notification scan. A saved cauldron-
  through-stone callback proves that the bounded capsule retains the second
  input block. Cake and cauldron registry opacity were corrected from the old
  cut-ID fallback to zero after strict raw-light negatives exposed each row.
  The first inventory-backed producer is also exact for ordinary
  non-loot-table chests. Oracle state captures each sorted 27-slot sparse
  tile; the capsule validates single-chest isolation or reciprocal adjacent
  double-chest halves, requires an unblocked represented block above, and
  materializes all halves before restoring comparator work. The runtime uses
  vanilla's per-slot stack-limit fullness formula. One full stone stack in
  one of 27 slots restores as output 1. Four full stacks in one half of a
  double chest are divided over all 54 slots and restore as output 2, not the
  single-half value 3. The clean single-chest
  negative first matched the inventory and queue but stayed at output 0; the
  behavior fix then exposed chest opacity 255 as a separate raw-light defect,
  corrected to the constructor-derived value zero. Furnaces now use the same
  source formula over their three slots and retain burn time, current burn
  time, cook time, and total cook time in the neutral capsule. A full stack in
  one furnace slot emits 5; the negative control isolates output 0 at
  observation 1 before the comparator/lamp mismatch. The existing fixed
  16-furnace pool is reused, and inventory changes notify only the bounded
  comparator-output scan on the promoted scripted insert/extract and furnace
  tick paths. Closed single trapped chests now reuse that 27-slot inventory
  formula while retaining their distinct block ID 146 and opacity zero. The
  capsule requires the `single_trapped_chest` schema, represented air above,
  no adjacent trapped half, and exactly zero viewers/lid motion; a full stack
  restores as output 1. Reciprocal trapped halves use the distinct
  `double_trapped_chest_half` schema and the same 54-slot composition as
  ordinary double chests: four stacks in one half restore as output 2, not
  the single-half value 3. Both deliberate omissions first diverge only at the
  comparator tile while exact container/queue state is retained.
  Live trapped-chest input now follows the integrated-server packet boundary:
  a use edge at tape tick 2 opens at observation 3, resets attack cooldown
  through the successful swing, increments the viewer count, and advances the
  exact lid float. Closing at tick 7 returns the viewer to zero and notifies
  both the chest and `pos.down()`. The resulting direct weak and
  upward-strong lamps retain two ordered block-124 callbacks through
  observations 7-9 and both turn off before observation 10. Ordinary chest
  viewers and horizontal strong-power-through-cube are native negative
  controls. Saved dispensers and droppers now use distinct exact nine-slot
  capsule schemas backed by one cold, dynamically grown pool capped at 256
  tiles. `Container.calcRedstoneFromInventory` is reproduced with Java-float
  arithmetic and the represented per-item stack limit: one full stack in one
  of nine slots emits 2 for both block ID 23 and inherited dropper ID 158.
  The deliberate dispenser omission retained exact inventory and queue state
  before first diverging only at comparator output on observation 1. Native
  controls cover empty output 0, an invalid slot-9 restore, and a
  non-stackable one-item dropper stack. The fixed oracle cases are exact for
  tile state, scheduled work, raw blocks, and light. The cold pool is
  allocated only when capsule state materializes; comparator queries inspect
  the exact coordinate and at most nine slots, with no idle tick scan.
  Jukebox ID 84 now reuses that cold pool as a one-record tile, but is
  validated separately: block metadata must agree with empty/present state;
  only untagged vanilla record IDs 2256..2267 with count 1/meta 0 are admitted.
  Comparator strength is `record_id - 2255`. Independent saved fixtures prove
  record 13 -> 1 and record wait -> 12, while native controls prove empty -> 0
  and reject non-record state. The pre-fix record-13 fixture retained exact
  tile and queue state and first diverged only at observation-1 comparator
  output. Record playback and sound emission are intentionally still A-01.
  Command blocks 137/210/211 now have a deliberately narrower exact saved
  subset. Oracle capture requires empty command text, name `@`, tracking on,
  no last output or command-result stats, `powered=false`,
  `conditionMet=false`, the vanilla per-ID `auto` default, no pending command
  callback, and `successCount` in 0..15. The capsule validates a distinct
  inventory-free schema and the matching block ID, then restores the count
  through a cold dynamically grown pool capped at 256 tiles. The deliberate
  omission at
  `trace/out/redstone_comparator_saved_command_block_probe_2/summary.md`
  retains exact tile/comparator/queue state and first diverges only at
  observation 1, where Java commits output 7 and Magma remains at zero.
  Corrected impulse/repeating/chain fixtures all commit output 7 with exact
  block and light state; native controls additionally prove zero output,
  reject count 16, and retire the tile on replacement. This slice does not
  execute commands and adds no command tick path or idle scan.
  Item-frame comparator input is now exact for a deliberately bounded saved
  subset. Vanilla checks the frame only when the immediate rear block is a
  normal cube, the direct input is below 15, the second cell is air, and
  exactly one item frame at that cell faces the comparator direction. An
  empty represented frame emits zero; a plain-stone stack with rotation
  `0..7` emits `rotation + 1`. Java capture records the frame's entity ID,
  pose, hanging position, facing, item tuple, and rotation in a complete
  dedicated list. The capsule proves its hanging air cell, normal-cube
  support, orientation, uniqueness, and pending comparator callback before
  restoring it into Magma's cold 256-entry pool.
  The deliberate omission at
  `trace/out/redstone_comparator_saved_item_frame_probe_1/summary.md`
  retained exact prestate, frame, comparator tile, and queue, then first
  diverged at observation 1 with only the comparator and lamp blocks
  mismatching. The corrected rotation-6 fixture passes at
  `trace/out/redstone_comparator_saved_item_frame_fix_1/summary.md`;
  native controls prove rotation 6 -> 7, rotation 7 -> 8, empty -> 0,
  rotation-8 rejection, non-plain item rejection, and retirement when either
  hanging cell or support changes. The 32-case family at
  `trace/out/matrix_redstone_comparator_item_frame_family_1/summary.md`
  passes 32/32, and the 133-case aggregate at
  `trace/out/matrix_redstone_comparator_item_frame_full_1/summary.md`
  passes every state, behavior, and raw-block gate with 21 matching features
  per row. This slice adds no entity tick scan and does not claim frame
  damage, drops, map/tag state, rendering, or general lifecycle.
  Observer ID 218 now implements the complete bounded six-facing pulse
  lifecycle. Only a mutation at the watched face schedules the activation
  callback at +2/priority 0; activation sets the powered bit and schedules the
  release at +2; output is weak and strong 15 only opposite the watched face.
  Duplicate watched edits while pending and edits while powered are
  suppressed. Live placement starts the same pulse, observer chains retain
  Java's same-time callback order, a proof-safe saved pending activation
  survives capsule restore, and breaking a powered observer with its release
  pending notifies the complete output neighborhood, including an indirect
  lamp through one normal cube. Native negatives cover non-watched edits and
  invalid facings. Early observer fixtures explicitly drain 8 or 12 controlled
  setup ticks so placement pulses cannot contaminate observation zero; the
  rejected pre-drain result remains at
  `trace/out/matrix_redstone_observer_six_faces_1/summary.md`. The six-face
  rerun passes 6/6 at
  `trace/out/matrix_redstone_observer_six_faces_2/summary.md`, the focused
  family passes 12/12 at
  `trace/out/matrix_redstone_observer_family_1/summary.md`, and the promoted
  145-case aggregate at
  `trace/out/matrix_redstone_observer_full_1/summary.md` passes every state,
  behavior, and raw-block gate. Mutation/callback paths inspect only the six
  adjacent cells; no observer world scan or idle allocation was added.
  Double trapped-chest GUI composition, transient open state in the saved
  capsule, deferred loot, command execution, and broader item-frame lifecycle
  remain separate. R-04 pistons are next.
- R-04 begins with the smallest complete moving-tile lifecycle rather than an
  eventual-state shortcut. A normal piston ID 33 in each of its six facings,
  powered from a non-output side by a tick-zero redstone block with air in
  front, changes its base metadata to the corresponding extended state and
  creates moving block 36 with matching facing metadata in the first
  observation. The fixed 64-entry active set records the piston tile's moved
  head, facing, source/extending flags, `lastProgress`, and `progress`.
  Progress is 0.5 after the first tile tick, 1.0 after the second, and the
  third replaces block 36 with settled head 34:5 and retires the entry.
  Piston block events remain distinct from the scheduled-tick list. The
  deliberate omission at
  `trace/out/redstone_piston_east_empty_extension_probe_1/summary.md`
  has an exact shared prestate and first diverges at the extended base, with
  exactly the absent base/head pair. The corrected first observation passes
  at
  `trace/out/redstone_piston_east_empty_extension_fix_1/summary.md`.
  Native tests loop over all six facing/offset pairs and gate all three EAST
  progress boundaries plus the immovable-obsidian negative. The initial
  four-case family remains at
  `trace/out/matrix_redstone_piston_empty_extension_family_1/summary.md`;
  the expanded family at
  `trace/out/matrix_redstone_piston_empty_extension_six_faces_family_1/summary.md`
  passes 9/9. The promoted 154-case aggregate at
  `trace/out/matrix_redstone_piston_empty_extension_six_faces_full_1/summary.md`
  passes every state, behavior, and raw-block gate.
  A separate tick-zero fixture flips a floor lever beside the EAST piston
  from metadata 5 to 13. Its deliberate omission retains an exact shared
  prestate and first diverges only at the unextended base plus absent moving
  head in
  `trace/out/redstone_piston_east_lever_empty_extension_probe_1/summary.md`.
  The corrected case passes at
  `trace/out/redstone_piston_east_lever_empty_extension_fix_1/summary.md`;
  the expanded ten-case piston family at
  `trace/out/matrix_redstone_piston_powered_lever_family_1/summary.md`
  passes 10/10, and the promoted 155-case aggregate at
  `trace/out/matrix_redstone_piston_powered_lever_full_1/summary.md`
  passes every state, behavior, and raw-block gate. Native controls also prove
  that metadata-5 lever state cannot start extension.
  Stone and wooden buttons now have independent tick-zero metadata 5-to-13
  proofs. Their deliberate omissions retain exact source/prestate and first
  diverge only at the base/head pair in
  `trace/out/redstone_piston_east_button_empty_extension_probe_1/summary.md`
  and
  `trace/out/redstone_piston_east_wooden_button_empty_extension_probe_1/summary.md`;
  corrected cases are in the corresponding
  `redstone_piston_east_button_empty_extension_fix_1` and
  `redstone_piston_east_wooden_button_empty_extension_fix_1` directories.
  The 12-case direct-control family at
  `trace/out/matrix_redstone_piston_direct_controls_family_1/summary.md`
  and promoted 157-case aggregate at
  `trace/out/matrix_redstone_piston_direct_controls_full_1/summary.md`
  pass every gate. Native positive/negative controls cover both button IDs.
  All four pressure-plate IDs then receive independent metadata setter edges:
  stone/wood change 0-to-1, light weighted changes 0-to-7, and heavy weighted
  changes 0-to-1. The four deliberate omissions at
  `trace/out/matrix_redstone_piston_pressure_plates_probe_1/summary.md`
  retain exact source/prestate and each diverge only at the same base/head
  pair. The corrected matrix at
  `trace/out/matrix_redstone_piston_pressure_plates_fix_1/summary.md`
  passes 4/4; the 16-case direct-power family at
  `trace/out/matrix_redstone_piston_direct_power_sources_family_1/summary.md`
  and promoted 161-case aggregate at
  `trace/out/matrix_redstone_piston_direct_power_sources_full_1/summary.md`
  pass every gate. Native tests loop all four IDs, nonzero strength values,
  settlement, and zero-strength negatives. Entity collision and plate
  rescheduling remain owned by their existing independent exact suites.
  The next pair isolates lit-torch face directionality. A floor torch south of
  the EAST piston supplies weak power to its north neighbor and starts the
  same empty extension, while a floor torch directly above the piston is
  queried on its attachment face and must not power it. The deliberate probe
  at
  `trace/out/matrix_redstone_piston_torch_direction_probe_1/summary.md`
  first shows only the positive case's absent extended-base/moving-head pair.
  The negative behavior is already exact, but its raw-light gate exposes an
  independent earlier defect: Java carries block light 6 through the
  non-opaque piston base while magma had zero. Piston IDs 29/33/34/36 now use
  vanilla opacity zero, and the isolated negative light correction passes at
  `trace/out/redstone_piston_east_torch_attached_face_light_fix_1/summary.md`.
  The piston power query now reuses the existing metadata-derived torch-face
  predicate; the corrected positive/negative pair passes 2/2 at
  `trace/out/matrix_redstone_piston_torch_direction_fix_1/summary.md`.
  Native tests cover start, settlement, wrong-face rejection, and exact
  block-light 6. The affected family passes 18/18 at
  `trace/out/matrix_redstone_piston_directional_torch_family_1/summary.md`,
  and the promoted aggregate passes 163/163 at
  `trace/out/matrix_redstone_piston_directional_torch_full_1/summary.md`
  with 163 state gates, 160 required behavior gates plus three not-required
  rows, and 163 raw-block gates.
  A powered-repeater pair then reuses the same source cell. Repeater 94:0
  points its north output at the piston and must extend it; repeater 94:1 is
  powered from the west but outputs east and must not. Stable redstone blocks
  behind each repeater keep both powered states valid. The deliberate
  three-case probe at
  `trace/out/matrix_redstone_piston_repeater_direction_probe_1/summary.md`
  retains the prior torch negative as a control, passes both directional
  negatives, and isolates the oriented repeater omission to exactly the
  unextended base and absent moving head. All 21 supported state features,
  the empty queue, source placement, and lighting are exact. The piston query
  now accepts powered repeater ID 94 only when its metadata-derived output
  matches the queried face. Native tests cover start, settlement, and rotated
  rejection; the corrected three-case matrix passes at
  `trace/out/matrix_redstone_piston_repeater_direction_fix_1/summary.md`.
  The affected family passes 20/20 at
  `trace/out/matrix_redstone_piston_directional_repeater_family_1/summary.md`,
  and the promoted aggregate passes 165/165 at
  `trace/out/matrix_redstone_piston_directional_repeater_full_1/summary.md`
  with 165 state gates, 162 required behavior gates plus three not-required
  rows, and 165 raw-block gates.
  Comparator directionality is tested at a stricter saved-state boundary:
  powered comparator 150 and exact tile output 15 are restored first, then
  piston 33:5 is placed at tick zero. Comparator 150:0 outputs north and
  extends; rotated 150:1 outputs east and must not. The deliberate probe at
  `trace/out/matrix_redstone_piston_comparator_direction_probe_1/summary.md`
  isolates the positive case to the same two piston cells, while the rotated
  case has exact state, tile, queue, and raw blocks but reveals an independent
  earlier light defect: Java's registered powered comparator emits 9 and the
  legacy table emitted zero. The corrected rotated light proof passes all
  10,625 cells at
  `trace/out/redstone_piston_east_comparator_wrong_direction_light_fix_1/summary.md`.
  The piston query now requires powered comparator state, positive saved tile
  output, and a matching output face. Native tests add oriented start and
  settlement, rotated rejection, and zero-output rejection; the corrected
  three-case matrix passes at
  `trace/out/matrix_redstone_piston_comparator_direction_fix_1/summary.md`.
  The affected family passes 22/22 at
  `trace/out/matrix_redstone_piston_directional_comparator_family_1/summary.md`,
  and the promoted aggregate passes 167/167 at
  `trace/out/matrix_redstone_piston_directional_comparator_full_1/summary.md`
  with 167 state gates, 164 required behavior gates plus three not-required
  rows, and 167 raw-block gates.
  Observer directionality is then proved with a live pulse rather than an
  invalid saved-powered state. Each fixture starts unpowered and drains the
  placement pulse for eight setup ticks. Tick zero places piston 33:5 and tick
  one edits the watched neighbor. Observer 218:3 watches south and outputs
  north into the piston; rotated 218:5 watches and outputs east, so it must
  leave the piston retracted. The first saved-powered attempt at
  `trace/out/matrix_redstone_piston_observer_direction_probe_1/summary.md`
  is retained as invalid fixture evidence: vanilla `onBlockAdded` clears the
  supplied powered bit before the shared prestate. The corrected deliberate
  omission at
  `trace/out/matrix_redstone_piston_observer_direction_probe_2/summary.md`
  has exact shared prestate, watched edit, 218:3-to-218:11 pulse metadata,
  one observer callback sequence, all 21 supported state features, and light;
  only the positive case differs, at exactly the unextended base and absent
  moving head. The piston query now consumes the existing metadata-derived
  observer output only during the powered pulse. Native regressions cover the
  live oriented start and settlement plus rotated rejection; the corrected
  comparator control and observer pair pass 3/3 at
  `trace/out/matrix_redstone_piston_observer_direction_fix_1/summary.md`.
  The affected family passes 24/24 at
  `trace/out/matrix_redstone_piston_directional_observer_family_1/summary.md`,
  and the promoted aggregate passes 169/169 at
  `trace/out/matrix_redstone_piston_directional_observer_full_1/summary.md`
  with 169 state gates, 166 required behavior gates plus three not-required
  rows, and 169 raw-block gates. The idle hot path remains one
  `piston_count == 0` branch; there is no world scan or allocation. This
  bounded slice then adds direct dust and one-hop normal-cube relay. Powered
  dust immediately south of the piston connects toward a redstone block and
  emits along that north-south axis; the rotated control has the same
  represented power but an east-west line and cannot drive the piston. The
  indirect positive puts powered dust on top of the south-adjacent stone, so
  dust strong-power reaches the Java-normal cube and the cube relays it. The
  indirect negative keeps that stone but moves dust beyond it on an east-west
  axis that does not strongly power the cube. Before implementation, the
  observer control and both negatives pass, while each positive differs at
  exactly the unextended base and absent moving head in
  `trace/out/matrix_redstone_piston_wire_indirect_probe_1/summary.md`.
  The piston query now reuses the existing exact dust weak-power predicate and
  normal-cube strong-power helper. Native tests cover both start/settlement
  paths and both negatives; the corrected five-case matrix passes at
  `trace/out/matrix_redstone_piston_wire_indirect_fix_1/summary.md`.
  The affected family passes 28/28 at
  `trace/out/matrix_redstone_piston_directional_wire_indirect_family_1/summary.md`,
  and the promoted aggregate passes 173/173 at
  `trace/out/matrix_redstone_piston_directional_wire_indirect_full_1/summary.md`
  with 173 state gates, 170 required behavior gates plus three not-required
  rows, and 173 raw-block gates. The idle hot path remains one branch and both
  helpers are bounded to the already-notified piston neighborhood. This does
  not yet claim the piston-block/modded self-power boundary, multi-block
  traversal, retraction, sticky pistons, slime, or entity collision.
  The next three geometry fixtures copy `BlockPistonBase.shouldBeExtended`
  literally. A redstone block in the EAST output/front cell is excluded by
  the direct loop. A block one up and one south lies in the fixed `pos.up()`
  quasi-connectivity neighborhood and extends, while its below-diagonal mirror
  does not. The deliberate four-case probe at
  `trace/out/matrix_redstone_piston_quasi_connectivity_probe_1/summary.md`
  passes the prior indirect control and both new negatives; only the quasi
  positive differs, at exactly the base/head pair. The corrected matrix at
  `trace/out/matrix_redstone_piston_quasi_connectivity_fix_1/summary.md`
  passes 4/4, native tests cover start/settlement and both negatives, the
  expanded family passes 31/31 at
  `trace/out/matrix_redstone_piston_quasi_connectivity_family_1/summary.md`,
  and the aggregate passes 176/176 at
  `trace/out/matrix_redstone_piston_quasi_connectivity_full_1/summary.md`.
  A single pushable stone is then isolated in front of the same EAST piston.
  Java creates two moving block-36 tiles at start, retains them through the
  second observation, and on the third settles head 34:5 in front plus stone
  1:0 at the destination. All three deliberate rows at
  `trace/out/matrix_redstone_piston_single_stone_push_probe_1/summary.md`
  retain exact shared prestate, power source, queue, state, and light and
  differ only at base/front/destination. Native tests and the corrected
  four-case matrix at
  `trace/out/matrix_redstone_piston_single_stone_push_fix_1/summary.md`
  prove start, progress, and settlement. The affected family passes 34/34 at
  `trace/out/matrix_redstone_piston_single_stone_push_family_1/summary.md`,
  and the promoted aggregate passes 179/179 at
  `trace/out/matrix_redstone_piston_single_stone_push_full_1/summary.md`
  with 179 state gates, 176 required behavior gates plus three not-required
  rows, and 179 raw-block gates. The fixed active set now uses two entries
  only while that represented push moves; the idle path is unchanged. This
  does not yet claim arbitrary push reactions, destroy reactions, retraction,
  sticky pistons, slime, save/reload mid-motion, rendering, or entity
  collision.
  Straight-stone traversal then extends the same lifecycle without a
  full-world search. A two-stone positive proves far-to-near movement and
  exact third-observation settlement. Boundary fixtures prove that a line of
  12 stones starts with 13 fixed-pool moving tiles (12 stones plus the head),
  while 13 stones leave the base and complete line unchanged. The earliest
  exploratory long-line fixture at
  `trace/out/matrix_redstone_piston_stone_line_limit_probe_1/summary.md`
  crossed the staged player and is retained as rejected fixture evidence.
  Moving it away from the player exposed a second fixture defect: the first
  12-stone destination landed outside the cleared platform on a generated
  leaf. That evidence and the corresponding first fix run remain preserved at
  `trace/out/matrix_redstone_piston_stone_line_limit_probe_2/summary.md` and
  `trace/out/matrix_redstone_piston_stone_line_limit_fix_1/summary.md`; only
  their uncontaminated two-stone and 13-stone rows are diagnostic. The final
  x=5 fixtures keep every source, stone, and destination inside the verified
  cuboid. Their corrected matrix passes 5/5 at
  `trace/out/matrix_redstone_piston_stone_line_limit_fix_2/summary.md`.
  Native tests cover two-stone start/settlement, all 13 moving tiles and final
  positions at the legal maximum, and exact rejection/intactness above the
  limit. The expanded affected family passes 38/38 at
  `trace/out/matrix_redstone_piston_stone_line_limit_family_1/summary.md`.
  The promoted aggregate passes 183/183 at
  `trace/out/matrix_redstone_piston_stone_line_limit_full_1/summary.md`
  with 183 state gates, 180 required behavior gates plus three not-required
  rows, and 183 raw-block gates in 824.064 seconds. The traversal is bounded
  to 13 forward reads and at most 13 fixed-pool entries only when a represented
  piston is notified; the one-branch empty idle path is unchanged.
  The next reaction slice adds exact dandelion 37:0 DESTROY behavior both
  directly in front and after one moved stone, including the spawned
  EntityItem, Java entity-before-moving-tile tick order, and six-direction
  swept item collision against the moving head/full cube. Displacement uses
  vanilla's overlap plus 0.01 and the per-axis 0.51 piston clamp. Birch planks
  5:2 independently prove that a registry-backed non-stone NORMAL block
  preserves ID and metadata through movement and settlement; obsidian remains
  the BLOCK control. The focused reaction family passes 42/42 at
  `trace/out/matrix_redstone_piston_block_reactions_family_1/summary.md`, and
  the corrected aggregate passes 187/187 at
  `trace/out/matrix_block_reactions_full_rerun_1/summary.md` with 187 state
  gates, 184 required behavior gates plus three not-required rows, and 187
  raw-block gates in 936.851 seconds. Native regressions cover exact front and
  terminal drop displacement plus all six moving directions.
  The Java harness now drains setup-only entities to a quiet pre-fixture
  boundary, makes ordinary falling-block entities explicit per case, restores
  World.rand/Math.random/entity-ID cursors immediately before the controlled
  mutation, and drains queued piston events at the intended server boundary.
  The survival water fixtures capture their actual pending IDs 8/9, retaining
  exact results after that stricter setup boundary. Broader DESTROY payloads,
  BLOCK states, non-full moving shapes, non-item entities, retraction, sticky
  pistons, slime structures, moving-tile capsule restore, and rendering remain
  open.
  A follow-on deliberate allium probe at
  `trace/out/redstone_piston_front_allium_destroy_probe_1/summary.md`
  first diverges only because the prior dandelion-only boundary rejects red
  flower 38:2. `BlockFlower.damageDropped` preserves its variant metadata;
  the generalized valid flower path now accepts red-flower metadata 0..8 and
  retains ID/meta in the spawned item. The focused fix passes at
  `trace/out/redstone_piston_front_allium_destroy_fix_1/summary.md`, the
  expanded piston family passes 43/43 at
  `trace/out/matrix_redstone_piston_flower_destroy_family_1/summary.md`, and
  the complete aggregate passes 188/188 at
  `trace/out/matrix_redstone_piston_flower_destroy_full_1/summary.md` with
  188 state gates, 185 required behavior gates plus three not-required rows,
  and 188 raw-block gates. This remains correctness-qualified rather than
  performance-promoted while unrelated host workloads keep the frozen CPU
  and CPU-fed renderer gates below their existing floors.
  The next deliberate fixture distinguishes torch block orientation from item
  damage. Java destroys a supported floor torch 50:5 but drops item 50:0; the
  omission at
  `trace/out/redstone_piston_front_floor_torch_destroy_probe_1/summary.md`
  is confined to the expected retracted base, intact torch, and missing item.
  The runtime now uses an explicit validated DESTROY payload mapping rather
  than blindly copying raw block metadata. Flowers retain their variant
  damage while every represented torch orientation 50:1..5 maps to item
  damage zero. The focused fix passes at
  `trace/out/redstone_piston_front_floor_torch_destroy_fix_1/summary.md`, the
  piston family passes 44/44 at
  `trace/out/matrix_redstone_piston_torch_destroy_family_1/summary.md`, and
  the aggregate passes 189/189 at
  `trace/out/matrix_redstone_piston_torch_destroy_full_1/summary.md` with 189
  state/raw-block gates, 186 required behavior gates, and three not-required
  rows.
  Redstone wire supplies the next distinct payload boundary: block 55 does
  not drop itself, but `BlockRedstoneWire.getItemDropped` returns registered
  item 331 with damage zero. The deliberate omission at
  `trace/out/redstone_piston_front_wire_destroy_probe_1/summary.md` and fix at
  `trace/out/redstone_piston_front_wire_destroy_fix_1/summary.md` isolate that
  block-to-item-ID mapping. The piston family passes 45/45 at
  `trace/out/matrix_redstone_piston_wire_destroy_family_1/summary.md`; the
  aggregate passes 190/190 at
  `trace/out/matrix_redstone_piston_wire_destroy_full_1/summary.md` with 190
  state/raw-block gates, 187 required behavior gates, and three not-required
  rows.
  Fire supplies the first represented DESTROY state with no drop at all.
  `BlockFire.quantityDropped` returns zero, so Java destroys supported fire
  without consuming drop RNG, allocating an EntityItem, or advancing the
  entity-ID cursor. The proof-safe fixture pushes one stone into terminal
  fire and retains the fire's already-pending block-51 callback. The
  deliberate rejection at
  `trace/out/redstone_piston_stone_then_fire_destroy_probe_1/summary.md`
  and exact fix at
  `trace/out/redstone_piston_stone_then_fire_destroy_fix_1/summary.md`
  isolate that semantic. The runtime distinguishes unsupported, no-item, and
  spawning-item payloads. The piston family passes 46/46 at
  `trace/out/matrix_redstone_piston_fire_no_drop_family_1/summary.md`; the
  aggregate passes 191/191 at
  `trace/out/matrix_redstone_piston_fire_no_drop_full_1/summary.md` with 191
  state/raw-block gates, 188 required behavior gates, and three not-required
  rows.
  Snow layers add a Forge-specific filtered-candidate path. `BlockSnow`
  metadata 3 represents four layers, and its state-sensitive `getDrops`
  creates five snowball stacks. Forge's piston patch deliberately passes
  chance -1 to preserve vanilla no-snowball behavior, so Java consumes five
  World.rand chance draws while spawning nothing. The deliberate rejection
  and exact fix are at
  `trace/out/redstone_piston_snow_multidrop_probe_1/summary.md` and
  `trace/out/redstone_piston_snow_suppressed_drop_fix_1/summary.md`.
  Native evidence locks the fifth internal LCG state while proving unchanged
  Math.random and entity-ID cursors. The piston family passes 47/47 at
  `trace/out/matrix_redstone_piston_snow_suppressed_drop_family_1/summary.md`;
  the aggregate passes 192/192 at
  `trace/out/matrix_redstone_piston_snow_suppressed_drop_full_1/summary.md`
  with 192 state/raw-block gates, 189 required behavior gates, and three
  not-required rows.
  Brown and red mushrooms complete the first shared deterministic default-drop
  class. Both inherit `Block.getDrops`: quantity one, item from the registered
  block, and damage zero. Separate mycelium-supported Java fixtures lock item
  39:0 and 40:0 independently. The deliberate pair and exact fix are at
  `trace/out/redstone_piston_mushroom_destroy_probe_1/summary.md` and
  `trace/out/redstone_piston_mushroom_destroy_fix_1/summary.md`. Native tests
  cover both IDs and their moving-head sweeps. The piston family passes 49/49
  at `trace/out/matrix_redstone_piston_mushroom_destroy_family_1/summary.md`;
  the aggregate passes 194/194 at
  `trace/out/matrix_redstone_piston_mushroom_destroy_full_1/summary.md` with
  194 state/raw-block gates, 191 required behavior gates, and three
  not-required rows.
  An attached east-facing ladder 65:5 then proves the same
  orientation-stripping payload rule under an indirectly queued piston
  event. The deliberate engine omission is at
  `trace/out/redstone_piston_attached_ladder_destroy_probe_1/summary.md`.
  The first engine fix made raw blocks and behavior exact but exposed a
  harness boundary error at
  `trace/out/redstone_piston_attached_ladder_destroy_fix_1/summary.md`:
  tick-zero redstone-block placement queued the neighboring piston event, but
  the Java bridge drained immediately only when the edited block itself was a
  piston. The deferred event therefore ran after excluded world/client work
  had advanced restored World.rand, Math.random, and next-entity-ID cursors.
  The bridge now drains the edit's block-event queue unconditionally at the
  same restored input boundary; an empty queue is a no-op. The exact focused
  result is
  `trace/out/redstone_piston_attached_ladder_destroy_fix_2/summary.md`.
  Native tests lock item 65:0 and the swept trajectory. The piston family
  passes 50/50 at
  `trace/out/matrix_redstone_piston_ladder_destroy_family_1/summary.md`; the
  aggregate passes 195/195 at
  `trace/out/matrix_redstone_piston_ladder_destroy_full_1/summary.md` with
  195 state/raw-block gates, 192 required behavior gates, and three
  not-required rows.
  Cobweb 30:0 then supplies a support-independent block-to-item mapping:
  `BlockWeb.getItemDropped` returns string item 287:0. The exact shared
  prestate and deliberate tick-zero omission are at
  `trace/out/redstone_piston_cobweb_destroy_probe_1/summary.md`; Java extends,
  replaces the web with moving head 36:5, and emits one exact string item
  while magma leaves the piston retracted and web intact. The explicit
  payload mapping and native item/sweep regression pass at
  `trace/out/redstone_piston_cobweb_destroy_fix_1/summary.md`. The piston
  family passes 51/51 at
  `trace/out/matrix_redstone_piston_cobweb_destroy_family_1/summary.md`; the
  aggregate passes 196/196 at
  `trace/out/matrix_redstone_piston_cobweb_destroy_full_1/summary.md` with
  196 state/raw-block gates, 193 required behavior gates, and three
  not-required rows.
  Ordinary and lit pumpkins 86:3/91:3 then prove the shared
  `BlockPumpkin` rule: horizontal facing metadata 0..3 is state only,
  inherited quantity is one, and inherited damage is zero. The deliberate
  pair at `trace/out/redstone_piston_pumpkin_destroy_probe_1/summary.md`
  isolates both tick-zero omissions from exact shared prestates. The explicit
  two-ID payload mapping and native regressions pass at
  `trace/out/redstone_piston_pumpkin_destroy_fix_1/summary.md`, including
  exact items 86:0/91:0, RNG-derived entity fields, moving-head sweeps, and
  complete emitted-light removal for ID 91. The piston family passes 53/53 at
  `trace/out/matrix_redstone_piston_pumpkin_destroy_family_1/summary.md`; the
  aggregate passes 198/198 at
  `trace/out/matrix_redstone_piston_pumpkin_destroy_full_1/summary.md` with
  198 state/raw-block gates, 195 required behavior gates, and three
  not-required rows.
  Structure void 217:0 then proves an independent zero-item mechanism:
  `BlockStructureVoid.dropBlockAsItemWithChance` is an empty override rather
  than a zero quantity. The deliberate probe at
  `trace/out/redstone_piston_structure_void_destroy_probe_1/summary.md` has
  passing state parity and exact shared prestate; only the rejected base/front
  mutations differ. The focused fix and native no-drop-cursor regression
  pass at `trace/out/redstone_piston_structure_void_destroy_fix_1/summary.md`.
  The piston family passes 54/54 at
  `trace/out/matrix_redstone_piston_structure_void_destroy_family_1/summary.md`;
  the aggregate passes 199/199 at
  `trace/out/matrix_redstone_piston_structure_void_destroy_full_1/summary.md`
  with 199 state/raw-block gates, 196 required behavior gates, and three
  not-required rows.
  The next registry audit covers the complete admitted metadata domains for
  lever 69, stone/wood buttons 77/143, stone/wood/weighted pressure plates
  70/72/147/148, redstone torches 75/76, and repeaters 93/94. Captured
  1.11.2 source proves default quantity one and damage zero; lit and unlit
  redstone torches both drop item 76, while powered and unpowered repeaters
  both drop item 356. Eleven deliberate fixtures preserve the unsupported
  pre-fix result at
  `trace/out/redstone_piston_control_destroy_probe_1/summary.md`. The exact
  15-case payload-and-side-effect candidate is at
  `trace/out/redstone_piston_control_destroy_bundle_1/summary.md`; native
  regressions exhaust all 118 canonical admitted states and reject invalid
  raw metadata.
  Four powered-control fixtures additionally prove block-specific break
  notification geometry through an indirect lamp: floor lever, stone plate,
  lit floor torch, and powered repeater. The deliberate mixed failure is
  preserved at
  `trace/out/redstone_piston_control_break_notify_probe_1/summary.md`, and the
  exact four-case correction is at
  `trace/out/redstone_piston_control_break_notify_fix_2/summary.md`. The
  repeater fixture exposed the earlier causal divergence: Java performs the
  dropped EntityItem's ordinary SELF move against a half-extended piston head
  before the piston tile's swept move. The bounded item path now resolves
  Y/X/Z collisions against represented moving-piston and normal-cube shapes,
  while worlds without active items retain the original no-item call path.
  The first exact-source rerun retained 14/15 focused passes but exposed a new
  earliest divergence at
  `trace/out/redstone_piston_control_destroy_bundle_current_1/summary.md`.
  With a west-moving lever drop, Java collided against the settled ID-34
  piston-head plate at tick 3, stopped at x=14.125, and zeroed X motion; magma
  still represented only moving heads and normal cubes. Captured
  `BlockPistonExtension` source defines a two-box plate plus non-SHORT arm for
  all six facings. The active-item path now reuses those exact shapes for
  nearby settled heads, including their 0.25-block arm overhang. The exact
  focused correction is at
  `trace/out/redstone_piston_control_settled_head_fix_1/summary.md`, and a
  native regression locks the previously failing Java row.
  All 15 focused cases pass on the final source at
  `trace/out/redstone_piston_control_destroy_bundle_current_fix_1/summary.md`.
  The expanded piston family passes 69/69 at
  `trace/out/matrix_redstone_piston_control_settled_head_family_1/summary.md`;
  the exact-current-source aggregate passes 214/214 at
  `trace/out/matrix_redstone_piston_control_settled_head_full_1/summary.md`
  with 214 state/raw-block gates, 211 required behavior gates, and three
  not-required rows.
  Dead bush 32:0 then proves the first randomized-count multi-item algorithm.
  Captured `BlockDeadBush` source performs one `World.rand.nextInt(3)` and
  returns that many separate stick 280:0 stacks; each stack independently
  consumes the ordinary chance/offset draws, four Math.random doubles, and
  one entity ID. The deliberate count-two failure is preserved at
  `trace/out/redstone_piston_dead_bush_destroy_probe_1/summary.md`, and the
  focused exact correction passes at
  `trace/out/redstone_piston_dead_bush_destroy_fix_1/summary.md`. Native tests
  lock count two and both RNG/ID cursors, the count-zero branch, and atomic
  rejection when only one entity slot remains. The piston family passes
  70/70 at
  `trace/out/matrix_redstone_piston_dead_bush_family_1/summary.md`; the full
  exact-current-source matrix passes 215/215 at
  `trace/out/matrix_redstone_piston_dead_bush_full_1/summary.md` with 215
  state/raw-block gates, 212 required behavior gates, and three not-required
  rows.
  Sapling 6 then proves stateful item metadata across the complete canonical
  type/stage domain. Captured `BlockSapling.damageDropped` retains wood type
  0..5 and strips stage bit 8. Oak stage 0 and dark-oak stage 1 preserve both
  endpoint omissions at
  `trace/out/redstone_piston_sapling_destroy_probe_1/summary.md`; both focused
  fixes pass at
  `trace/out/redstone_piston_sapling_destroy_fix_1/summary.md`. Native tests
  exhaust all 12 canonical states and reject raw 6/7/14/15 without partial
  RNG, entity, piston, or world mutation. The piston family passes 72/72 at
  `trace/out/matrix_redstone_piston_sapling_family_1/summary.md`, and the full
  exact-current-source matrix passes 217/217 at
  `trace/out/matrix_redstone_piston_sapling_full_1/summary.md` with 217
  state/raw-block gates, 214 required behavior gates, and three not-required
  rows.
  A paired same-boundary fixture then exposes a hidden causal side effect in
  `BlockPistonBase.eventReceived`: after every successful extension, Java
  consumes one `World.rand.nextFloat()` for sound pitch even when audio output
  is disabled. One center redstone placement notifies opposed WEST/EAST
  pistons in order, so the second sapling item's offsets depend on exactly the
  intervening pitch draw with no integrated-client work between events. The
  deliberate omission fails only entity state at tick zero while raw blocks
  remain exact at
  `trace/out/redstone_piston_dual_sound_rng_probe_1/summary.md`; the focused
  correction passes at
  `trace/out/redstone_piston_dual_sound_rng_fix_1/summary.md`. Native tests
  lock two extensions, two items, the exact World.rand/Math.random/entity-ID
  cursors, and the no-item/count branches' revised pitch-only consumption.
  The piston family passes 73/73 at
  `trace/out/matrix_redstone_piston_dual_sound_rng_family_1/summary.md`; the
  complete matrix passes 218/218 at
  `trace/out/matrix_redstone_piston_dual_sound_rng_full_1/summary.md` with 218
  state/raw-block gates, 215 required behavior gates, and three not-required
  rows.
  Tall grass 31:0..2 then exposes a distinct process-global cursor:
  `Block.RANDOM`, whose private 48-bit `java.util.Random` state is causal for
  drops but is not serialized in vanilla world NBT. The oracle now captures
  it as `world_rng.block_seed48`, permits an exact parked-boundary seed, and
  restores it immediately before controlled tick-zero edits. State capsules
  validate and replay the same cursor into GmRuntime. Controlled internal seed
  0 takes the 1/8 success branch, then consumes the sole weight-10 seed-list
  choice and `nextInt(1)` count draw before spawning wheat seeds 295:0;
  internal seed 1396 takes the one-draw no-drop branch. Omitting only the
  otherwise invisible `nextInt(1)` leaves state features and raw blocks exact
  but fails the behavior gate at tick zero in
  `trace/out/redstone_piston_tall_grass_block_rng_probe_1/summary.md`. Both
  positive branches pass at
  `trace/out/redstone_piston_tall_grass_fix_1/summary.md`. Native tests cover
  all three canonical metadata values, raw metadata 3 rejection, both random
  branches, exact World.rand/Math.random/Block.RANDOM/entity-ID cursors, and
  atomic full-pool rejection. The piston family passes 75/75 at
  `trace/out/matrix_redstone_piston_tall_grass_family_1/summary.md`; the full
  exact-current-source matrix passes 220/220 at
  `trace/out/matrix_redstone_piston_tall_grass_full_1/summary.md` with 220
  state/raw-block gates, 217 required behavior gates, and three not-required
  rows.
  Wheat 59:0..7 then adds age-dependent `BlockCrops.getDrops`. Ages 0..6
  emit one seed 295:0 without count RNG; mature age 7 emits wheat 296:0,
  consumes exactly three `World.rand.nextInt(14)` trials, and creates one
  separate seed stack for every result at most 7. Controlled internal seed 0
  produces trial results 0/4/9, hence wheat plus two seed entities. The
  preimplementation negative at
  `trace/out/redstone_piston_mature_wheat_probe_3/summary.md` proves extension,
  items, and cursor state were absent on magma. The first implemented oracle
  comparison then exposed the earlier raw-world side effect: moving piston
  block 36 has solid material, so `BlockFarmland.neighborChanged` converts
  support 60:0 to dirt 3:0. The live registry export now includes both a
  canonical-metadata mask and `Material.isSolid` mask; invalid wheat metadata
  is rejected atomically and the farmland callback uses that exact predicate.
  Mature and immature focused cases pass at
  `trace/out/redstone_piston_wheat_fix_3/summary.md`, including exact item
  order/EIDs, raw blocks, and seeded after-cursors.
  The cursor harness now brackets every controlled edit. When both engines
  begin at the same cursor, it still requires the complete absolute bundle;
  after ambient Java work makes later starts differ, it compares exact raw
  Java-LCG transition counts plus entity-ID and 32-bit updateLCG deltas. The
  11 absolute-cursor false positives are retained in
  `trace/out/matrix_redstone_piston_wheat_full_1/summary.md`; a 13-case rerun
  passes at `trace/out/controlled_transition_fix_1/summary.md`, and the final
  exact-current-source matrix passes 222/222 at
  `trace/out/matrix_redstone_piston_wheat_full_2/summary.md` with all 222
  state/raw-block gates, 219 required behavior gates, and three not-required
  rows. The direct script regression also proves a deliberately wrong LCG
  draw count fails this causal gate.
  Leaves 18/161 now implement all canonical type/decay metadata aliases,
  exact sapling and oak/dark-oak apple chances, Java RNG order, capacity
  preflight, adjacent CHECK_DECAY marking, and the vanilla 9-cube/four-round
  log-connectivity decay scan. The pre-fix probes retain the missing piston
  payload and decay outcomes. Four focused piston cases pass at
  `trace/out/redstone_piston_leaves_fix_1/summary.md`, three natural decay
  cases pass at `trace/out/leaf_decay_fix_2/summary.md`, the combined focused
  set passes 7/7 at `trace/out/leaves_focused_fix_1/summary.md`, and the piston
  family passes 81/81 at
  `trace/out/matrix_redstone_piston_leaves_family_1/summary.md`. The complete
  current-source matrix passes 229/229 at
  `trace/out/matrix_redstone_piston_leaves_full_1/summary.md`: all state and
  raw-block gates, 226 required behavior gates, and three not-required rows.
  A valid two-block reed column then adds same-boundary support recursion.
  The lower 83:7 piston DESTROY emits registered item 338:0; its ordered
  notification makes upper 83:11 fail `canBlockStay`, emit a second exact
  item, become air, and continue notification upward. The pre-fix result at
  `trace/out/redstone_piston_reed_column_probe_1/summary.md` differs first at
  the retracted base; the focused fix passes at
  `trace/out/redstone_piston_reed_column_fix_1/summary.md`, the piston family
  passes 82/82 at
  `trace/out/matrix_redstone_piston_reed_column_family_1/summary.md`, and the
  promoted aggregate passes 230/230 at
  `trace/out/matrix_redstone_piston_reed_column_full_1/summary.md`. Native
  coverage includes atomic rejection when only one entity slot is free.
  Cactus 81:0..15 now applies the complete neighbor-stability predicate:
  sand/cactus support, no horizontal solid material or lava, and no liquid
  above. A moved stone does not invalidate adjacent cactus while represented
  by block 36; its tick-three settlement to real stone does, after the
  ordinary entity pass. The clean negative and focused correction are at
  `trace/out/redstone_piston_cactus_settlement_probe_2/summary.md` and
  `trace/out/redstone_piston_cactus_fix_2/summary.md`. A separate controlled
  neighbor placement proves exact item 81:0 metadata, World/Math RNG, EID,
  age, pickup delay, and trajectory without integrated-client cursor noise.
  The expanded piston family passes 84/84 at
  `trace/out/matrix_redstone_piston_cactus_family_1/summary.md`; the complete
  current-source matrix passes 232/232 at
  `trace/out/matrix_redstone_piston_cactus_full_1/summary.md`, with all 232
  state/raw-block gates, 229 required behavior gates, and three not-required
  rows. Extension preflight reserves the complete affected cactus column's
  fixed-pool capacity before any RNG, piston, entity, or world mutation.
  Chorus plant 199 and flower 200 now use their exact direct and horizontal
  support predicates, schedule support loss at +1, and preserve the resulting
  one-layer-per-tick plant cascade. Flower destruction emits no item; plant
  destruction consumes `World.rand.nextInt(2)` and emits chorus fruit 432:0
  only on the exact branch. Direct piston destruction, block-36 settlement,
  queue timing, RNG cursors, and full-pool rollback pass focused fixtures at
  `trace/out/chorus_support_fix_1/summary.md`,
  `trace/out/chorus_piston_payload_fix_1/summary.md`, and
  `trace/out/chorus_flower_settlement_fix_4/summary.md`. The expanded
  aggregate passes 241/241 at
  `trace/out/matrix_redstone_piston_chorus_full_1/`; all state and raw-block
  gates pass, with 235 required behavior gates and six not-required rows. The
  paired audit then made all canonical double-plant teardown branches exact.
  Lower rose emits 175:4, lower grass covers exact seed/no-drop `World.rand`
  branches, fern emits nothing, and upper-half destruction delegates the item
  to the lower half while removing both cells. The focused five-case proof is
  `trace/out/double_plant_piston_fix_1/summary.md`; the intermediate full
  aggregate passes 246/246 at
  `trace/out/matrix_redstone_piston_double_plant_full_1/summary.md`.
  Bed 26 foot-first and head-first destruction now remove both halves and emit
  exactly one bed item 355:0 from the foot-owned path. Both controlled cursor
  transitions and fixed-pool atomicity are covered at
  `trace/out/bed_piston_fix_1/summary.md`. The latest 32-client promotion run
  passes 248/248 in 209.437 seconds at
  `trace/out/matrix_redstone_piston_bed_full_1/summary.md`, with 242 required
  behavior gates and six not-required rows. All seven 1.11.2 door blocks are
  now exact for paired piston teardown across all 56 canonical lower states
  and 28 canonical upper states. Lower halves map to distinct registered door
  items 324, 330, and 427..431; upper halves emit nothing directly and defer
  the one item to the lower callback. The retained 8/8 negative and focused
  exact evidence is at `trace/out/redstone_piston_door_{probe,fix}_1/`. The
  32-client aggregate passes 256/256 in 219.355 seconds at
  `trace/out/matrix_redstone_piston_door_full_1/summary.md`, with 250 required
  behavior gates and six not-required rows. Occupied flower-pot 140 teardown
  is now exact as the first tile-bearing DESTROY branch. The versioned capsule
  carries its contained item and metadata, the cold runtime pool allocates
  only when used, and piston teardown emits Forge's ordered pot 390:0 plus
  contained item before retiring the tile. The retained first correction at
  `trace/out/redstone_piston_flower_pot_fix_1/summary.md` caught the missing
  contained drop; the corrected focused proof passes at
  `trace/out/redstone_piston_flower_pot_fix_2/summary.md`. The current
  32-client aggregate passes 255/255 in 202.281 seconds at
  `trace/out/matrix_redstone_piston_flower_pot_full_1/summary.md`, with 251
  required behavior gates and four not-required rows. The registry audit now
  includes all 96 ownerless skull type/rotation combinations. Skull tile type
  owns the ItemSkull 397 metadata, while rotation is retained only in the tile
  and both fields retire during teardown. Player-profile skull state now uses
  a bounded, checksummed uncompressed-NBT sidecar. The C cold pool retains the
  complete GameProfile compound and piston teardown wraps it under the dropped
  ItemStack's `SkullOwner` compound without losing UUID, name, properties, or
  signatures. NBT comparisons are typed and compound-order independent.
  Ownerless focused real-game
  evidence passes at `trace/out/redstone_piston_skull_fix_1/summary.md`; the
  current 32-client aggregate passes 256/256 in 206.681 seconds at
  `trace/out/matrix_redstone_piston_skull_full_1/summary.md`, with 252 required
  behavior gates and four not-required rows. Closed, unnamed, unlocked
  shulker boxes are now exact for all 16 colors, six facings, and plain
  27-slot inventories. Their DESTROY hook bypasses the ordinary chance draw,
  emits one colored unstackable ItemBlock with an exact `BlockEntityTag`, and
  retires the tile. The focused purple-box proof passes at
  `trace/out/redstone_piston_shulker_box_fix_2/summary.md`; the current
  32-client aggregate passes 257/257 in 218.740 seconds at
  `trace/out/matrix_redstone_piston_shulker_box_full_2/summary.md`, with 253
  required behavior gates and four not-required rows. The shared exact NBT
  path now also carries nested contained-item tags, custom names, locks, and
  deferred loot-table/seed state without materializing the loot inventory.
  Shulker ItemStack tags retain the complete `BlockEntityTag`, including typed
  slot widths, and duplicate `CustomName` into `display.Name` exactly as Java
  does. Focused rich-inventory and deferred-loot evidence passes 2/2 at
  `trace/out/redstone_piston_shulker_nbt_probe_2/summary.md`.
  The signed-profile focused proof passes at
  `trace/out/redstone_piston_player_skull_fix_2/summary.md`; the current
  32-client aggregate passes 258/258 in 217.468 seconds at
  `trace/out/matrix_redstone_piston_player_skull_full_1/summary.md`, with 254
  required behavior gates and four not-required rows. The current hardened
  32-client aggregate passes 260/260 in 222.136 seconds at
  `trace/out/matrix_redstone_piston_shulker_nbt_full_2/summary.md`, with 256
  required behavior gates, four not-required rows, and no retries.
  The exact 1.11.2 reaction audit now also proves representative BLOCK paths:
  anvil mobility, unbreakable end-portal frame, chest tile state, and an
  already-extended piston all reject extension without mutation. Flowing and
  static water/lava IDs 8..11 accept every metadata value as zero-drop
  DESTROY payloads; native coverage spans all 64 states and full-entity-pool
  rollback, while real Java source-water and source-lava fixtures prove exact
  base/head transitions and RNG. The current 32-client aggregate passes
  266/266 with no retries in 220.390 seconds at
  `trace/out/matrix_redstone_piston_fluid_block_full_1/summary.md`, with 262
  required behavior gates and four not-required rows.
  Cake 92 metadata 0..6 now follows its exact zero-drop path, including a
  three-bite analog-comparator circuit that clears output 8 at +2 and its lamp
  at +4. Moving piston 36 and settled head 34 are valid zero-strength rear
  comparator inputs. The 32-client cake aggregate passes 269/269 in 237.138
  seconds at `trace/out/matrix_redstone_piston_cake_full_2/summary.md`, with
  265 required behavior gates and four not-required rows. Melon block 103
  consumes its two count draws and emits three through seven separate item
  360:0 stacks with exact World/Math RNG and entity cursors. Controlled
  three- and seven-drop proofs pass 2/2 at
  `trace/out/redstone_piston_melon_fix_1/summary.md`. The current 32-client
  aggregate passes 271/271 with no retries in 214.462 seconds at
  `trace/out/matrix_redstone_piston_melon_full_1/summary.md`, with 267 required
  behavior gates and four not-required rows.
  Pumpkin/melon stems 104/105 consume exactly three process-global
  `Block.RANDOM.nextInt(15)` trials and emit one item 361:0/362:0 stack for
  each result at most the stem age. The moving head also notifies supporting
  farmland to become dirt. Focused zero- and maximum-drop evidence passes 3/3
  at `trace/out/redstone_piston_stem_fix_1/summary.md`; exhaustive native
  coverage includes both IDs, all ages, invalid metadata, one/two/three-drop
  counts, full-pool zero-drop success, and atomic insufficient-capacity
  rejection. The current 32-client aggregate passes 274/274 in 243.841
  seconds at `trace/out/matrix_redstone_piston_stem_full_1/summary.md`, with
  270 required behavior gates and four not-required rows. One unrelated
  button worker was recycled and passed its automatic retry.
  Vine 106 accepts all sixteen attachment masks but emits no item under an
  ordinary piston break. Waterlily 111 accepts all sixteen raw metadata values
  and emits one normalized item 111:0 while its supporting water remains.
  Nether wart 115 emits one item 372:0 at ages 0..2 and `2 + nextInt(3)`
  separate stacks at age 3. Focused vine/waterlily and wart evidence passes
  2/2 at `trace/out/redstone_piston_vine_waterlily_fix_1/summary.md` and 2/2 at
  `trace/out/redstone_piston_nether_wart_fix_1/summary.md`. Exhaustive native
  coverage includes all raw vine/waterlily metadata, all four wart ages,
  invalid wart metadata, both mature count boundaries, full-pool zero-drop
  success, and atomic insufficient-capacity rejection. The current 32-client
  aggregate passes 278/278 in 244.461 seconds at
  `trace/out/matrix_redstone_piston_vine_waterlily_wart_full_1/summary.md`, with
  274 required behavior gates and four not-required rows. One unrelated lava
  case passed after its isolated worker was recycled.
  Dragon egg 122 emits one normalized item 122:0. Cocoa 127 emits one brown
  dye 351:3 at ages 0/1 and three separate dyes at age 2. Focused exact
  evidence passes 1/1 at `trace/out/redstone_piston_dragon_egg_fix_1/summary.md`
  and 2/2 at `trace/out/redstone_piston_cocoa_fix_1/summary.md`; native tests
  exhaust valid metadata and atomic capacity rejection. The definitive
  aggregate passes 281/281 with no retries in 220.296 seconds at
  `trace/out/matrix_redstone_piston_cocoa_full_2/summary.md`, with 277 required
  behavior gates and four not-required rows. A scheduled-chorus RNG fixture
  ordering correction passed 16 independent pool repetitions before promotion.
- R-02 flat dust now has an edit-driven, allocation-free active component
  proof. One wire powers to metadata 15 and hands power to a lamp in the same
  tick; removal drains the wire immediately and preserves the lamp's separate
  +4 callback. A 15-wire line settles to 15 through 1 and powers its endpoint,
  while the 16th wire remains zero and leaves its endpoint lamp off. A
  T-branch settles to 15/14, and removing the source from an eight-wire loop
  drains every stale metadata value to zero. The promoted component is bounded
  to 256 wires on represented valid supports. The provenance-locked
  registry now captures `isFullyOpaque` for all 4,096 legacy ID/metadata states;
  Java's explicit glowstone exception is applied beside that exact mask. The
  deliberate old-C result leaves the glowstone-supported wire at zero and its
  lamp off while the stone control passes at
  `trace/out/matrix_wire_glowstone_probe_1/summary.md`; both pass with exact
  blocks and light at `trace/out/matrix_wire_glowstone_1/summary.md`. Separate
  top-slab, upside-down-stair, and stable eight-layer-snow probes fail first at
  the dust metadata, then pass 4/4 with glowstone at
  `trace/out/matrix_wire_fully_opaque_1/summary.md`. Native lower-half,
  incomplete-snow, and glass controls remain outside the proof. The component
  also includes vanilla's same-level, one-block climb and one-block descent
  through a non-normal adjacent cell. Flat components now remain active under
  a normal-cube ceiling. A climb accepts any non-normal lower-wire headroom,
  not only air, while a normal cube blocks that upward edge. The three old-C
  probes fail first at dust metadata and all pass with the original air climb
  at `trace/out/matrix_wire_headroom_1/summary.md`. Powered dust above stone
  also strongly powers that support. Horizontal weak output now consumes the
  same Java `isPowerSourceAt` shape predicate: same-level vanilla power
  providers, axis-only repeaters/comparators, facing-only observers, and dust
  reached one block above or below. The old-C climb probe lights a
  perpendicular lamp because it mistakes the vertical connection for an
  isolated dot; the connected control passes. Climb/descent connected and
  perpendicular controls plus wrong-axis/aligned repeater controls pass 6/6
  at `trace/out/matrix_wire_directional_output_candidate_1/summary.md`, and
  eight affected earlier circuits pass at
  `trace/out/matrix_wire_directional_output_regression_1/summary.md`.
  Removing an invalidated support now follows `neighborChanged`: it consumes
  the exact four `World.rand` spawn draws, creates redstone item 331 with the
  matching entity/Math RNG state, replaces the wire with air, recomputes the
  remaining component, and notifies delayed consumers. The old-C probe leaves
  the unsupported wire floating while its unrelated-neighbor control passes.
  Both plus a powered two-wire drain and lamp +4 lifecycle pass 3/3 at
  `trace/out/matrix_wire_support_loss_1/summary.md`.
  Dust recomputation temporarily disables wire output,
  matching vanilla's `canProvidePower=false` guard so source removal cannot
  leave self-power through the solid. Overflow and components leaving this
  proof region are rejected instead of partially simulated.
- R-02 floor torches now implement both +2 inverter edges. Powering the support
  schedules block 76, leaves it lit for observation 0, and changes 76:5 to
  75:5 on observation 1. Removing support power schedules block 75 and mirrors
  the same boundary back to 76:5. Torch opacity is zero and lit emission is 7,
  so both focused cases match all 10,625 light cells. The capsule promotes
  only a metadata-5 floor torch with its exact stone/redstone-block support
  and clear non-support neighbors; both saved edges preserve absolute due
  time, priority, and order. The eighth transition within 60 ticks burns out,
  consumes the exact Java RNG sequence, and recovers at +160. Metadata
  1/2/3/4 wall orientations are exact in both directions. A lit torch also
  exposes its directional strong output through the stone above it. A
  checkpoint with seven invisible toggle entries restores into magma and
  reproduces Java's continued eighth-toggle suffix exactly. Floor and wall
  torches now also validate their stored attachment on every neighbor update.
  Invalid support emits item 76:0 with exact spawn cursors before replacing
  either lit or unlit state with air; lit removal retains its second-ring
  redstone notifications. Forge's directional support rules cover slab and
  stair halves/shapes, full snow, farmland sides, hopper tops, fences, glass,
  stained glass, cobblestone walls, and powered compressed blocks. The old-C
  floor/wall probes leave ID 76 floating while the unrelated edit passes.
  All three focused cases pass at
  `trace/out/matrix_redstone_torch_support_loss_candidate_1/summary.md`, and
  eight affected inverter/strong-power/piston cases pass at
  `trace/out/matrix_redstone_torch_support_loss_regression_1/summary.md`.
- R-03 repeaters and comparators now validate Java's stateful
  `pos.down().isFullyOpaque()` stay predicate on every neighbor update.
  Unsupported blocks 93/94 normalize to item 356:0 and blocks 149/150 to item
  404:0 with exact RNG/entity cursors. Comparator tile state retires before
  directional output teardown, saved diode callbacks remain stale until due,
  and powered outputs hand lamps their independent +4 release. The five
  deliberate old-C failures plus the top-slab negative are exact 6/6 at
  `trace/out/matrix_redstone_diode_support_loss_candidate_5/summary.md`.
  Native state-shape, tile-lifecycle, stale-work, and full-pool tests pass at
  `trace/out/test_runtime_redstone_diode_support_loss.log`; GPU 1 performance
  passes at 5,042 scalar steps/s, 2.93M Blaze env-ticks/s, and 30.69 CUDA fps.
- R-02 tripwire hooks now validate their serialized horizontal attachment
  against Forge directional side solidity. An attached powered line detaches
  its opposite hook and wire states, orders both lamp +4 callbacks, retains its
  saved +10 hook callback as stale work, and drops item 131:0 exactly. The
  deliberate two-failure/one-negative fixture and all affected entity,
  projectile, piston, and player-crossing cases pass 14/14 at
  `trace/out/matrix_redstone_tripwire_hook_support_loss_affected_1/summary.md`.
  Native four-facing and full-pool coverage passes, and GPU 1 performance is
  5,065 scalar steps/s, 2.93M Blaze env-ticks/s, and 31.36 CUDA fps.
- R-02 direct tripwire edits now run the removed block's Java lifecycle after
  the replacement state is visible. Removing attached string produces an
  immediate two-hook pulse, one exact +10 hook callback, and east-then-west
  lamp release at +14. Removing an attached unpowered hook immediately
  detaches all remaining wire states and consumes the exact two `World.rand`
  pitch draws without emitting an item. The uncontaminated fixture begins
  attached with an empty queue after 12 controlled setup ticks. Both deliberate
  old-C failures and the corrected cases are retained at
  `trace/out/matrix_redstone_tripwire_live_break_probe_2/summary.md` and
  `trace/out/matrix_redstone_tripwire_live_break_candidate_2/summary.md`.
  The complete affected family passes 16/16 at
  `trace/out/matrix_redstone_tripwire_live_break_affected_1/summary.md`.
  Native coverage passes with a 280 MB compile-and-test peak, and GPU 1
  performance passes at 5,095 scalar steps/s, 2.93M Blaze env-ticks/s, and
  31.86 CUDA fps.
- R-02 tripwire on-add now scans Java's SOUTH-then-WEST hook directions after
  the new string state is visible. Filling the middle gap of a settled,
  detached line attaches both opposite-facing hooks and all three strings in
  the same tick, schedules only the west hook for +10, and drains that callback
  without changing the complete line. Isolated placement is the deliberate
  no-hook negative. Old magma fails only the completed-line case at
  `trace/out/matrix_redstone_tripwire_on_add_probe_1/summary.md`; both corrected
  cases pass at `trace/out/matrix_redstone_tripwire_on_add_candidate_2/summary.md`.
  The expanded tripwire family passes 18/18 at
  `trace/out/matrix_redstone_tripwire_on_add_affected_1/summary.md`. Native
  coverage passes with a 282 MB compile-and-test peak, and GPU 1 performance
  passes at 5,121 scalar steps/s, 2.94M Blaze env-ticks/s, and 32.04 CUDA fps.
- R-02 direct pressure-plate replacement now runs
  `BlockBasePressurePlate.breakBlock` for every nonzero binary or analog
  strength. The old state notifies both its own neighborhood and the support
  neighborhood after the plate becomes air, allowing an indirectly powered
  lamp beside the support to queue its exact +4 release. Stone, wood, gold,
  and iron plates all fail old magma; the unpowered stone negative passes at
  `trace/out/matrix_redstone_pressure_plate_direct_break_probe_1/summary.md`.
  All five corrected cases pass at
  `trace/out/matrix_redstone_pressure_plate_direct_break_candidate_2/summary.md`,
  and the complete affected plate family passes 21/21 at
  `trace/out/matrix_redstone_pressure_plate_direct_break_affected_1/summary.md`.
  Native coverage passes with a 283 MB compile-and-test peak. GPU 1 performance
  passes at 5,152 scalar steps/s, 2.93M Blaze env-ticks/s, and 31.42 CUDA fps.
- R-02 direct redstone-wire replacement now runs Java's removed-block
  notification ring after the dust becomes air. The six adjacent positions
  each notify their own neighbors before the ordinary outer replacement
  notification, so a lamp powered indirectly through the former wire support
  receives its exact +4 release callback. Powered 55:15 fails old magma while
  unpowered 55:0 is the no-callback negative at
  `trace/out/matrix_redstone_wire_direct_break_probe_1/summary.md`. Both fixed
  cases pass at
  `trace/out/matrix_redstone_wire_direct_break_candidate_2/summary.md`, and all
  35 represented wire, topology, support, strong-power, and piston cases pass
  at `trace/out/matrix_redstone_wire_direct_break_affected_1/summary.md`.
  Native coverage passes with a 282 MB peak. GPU 1 performance passes at 5,156
  scalar steps/s, 2.93M Blaze env-ticks/s, and 31.94 CUDA fps.
- R-03 direct repeater/comparator replacement now runs each diode's
  output-neighborhood teardown after the old state becomes air. Comparator
  tiles retire before that notification. Powered repeater 94:0 and comparator
  149:8 deliberately fail old magma at the missing indirect lamp callback;
  unpowered 93:0 and 149:0 are exact no-callback controls at
  `trace/out/matrix_redstone_diode_direct_break_probe_4/summary.md`. All four
  fixed cases pass at
  `trace/out/matrix_redstone_diode_direct_break_candidate_1/summary.md`, and 14
  lifecycle-adjacent direct, saved, support-loss, and piston cases pass at
  `trace/out/matrix_redstone_diode_direct_break_affected_1/summary.md`. Native
  coverage passes at a 286 MB peak. GPU 1 performance passes at 5,142 scalar
  steps/s, 2.93M Blaze env-ticks/s, and 30.83 CUDA fps.
- R-03 direct repeater placement now runs the shared diode `onBlockAdded`
  output notification after the new state is visible. A powered SOUTH-facing
  94:0 immediately lights a lamp adjacent only to its north output stone;
  unpowered 93:0 is the no-callback control. Old magma fails only the powered
  row at `trace/out/matrix_redstone_repeater_direct_add_probe_1/summary.md`.
  Both corrected rows pass at
  `trace/out/matrix_redstone_repeater_direct_add_candidate_1/summary.md`, and
  all ten direct, directional, delayed, and saved repeater cases pass at
  `trace/out/matrix_redstone_repeater_direct_add_affected_1/summary.md`. Native
  coverage passes at a 286 MB peak. GPU 1 performance passes at 5,145 scalar
  steps/s, 2.93M Blaze env-ticks/s, and 30.66 CUDA fps.
- R-03 direct comparator placement now runs the inherited directional output
  callback before creating its output-zero tile, matching Java's
  `BlockRedstoneComparator.onBlockAdded` order. Aligned 149:1 placement makes
  a stale downstream powered repeater queue its +2 release and hand the lamp
  a +6 release; rotated 149:3 is the empty-queue control. The aligned row
  deliberately fails old magma while the control passes at
  `trace/out/matrix_redstone_comparator_direct_add_probe_2/summary.md`. Both
  fixed rows pass at
  `trace/out/matrix_redstone_comparator_direct_add_candidate_1/summary.md`,
  and all 14 direct, scheduled, saved, support-loss, and piston comparator
  cases pass at
  `trace/out/matrix_redstone_comparator_direct_add_affected_1/summary.md`.
  Native coverage passes at a 285 MB peak. GPU 1 performance passes at 5,030
  scalar steps/s, 2.93M Blaze env-ticks/s, and 31.66 CUDA fps.
- R-02 direct wire placement now runs the complete
  `BlockRedstoneWire.onBlockAdded` notification traversal after component
  recomputation. A zero-power wire above a support reaches a diagonal stale
  powered repeater through the DOWN-centered callback and queues its +2
  release; the unpowered counterpart is queue-free. With only the two new
  call sites disabled, the powered row fails while the control passes at
  `trace/out/matrix_redstone_wire_direct_add_probe_5/summary.md`. Both fixed
  rows pass at
  `trace/out/matrix_redstone_wire_direct_add_candidate_2/summary.md`, and all
  14 flat, vertical, branch, support, removal, repeater, and comparator cases
  pass at
  `trace/out/matrix_redstone_wire_direct_add_affected_1/summary.md`. Native
  coverage passes at a 287 MB peak. GPU 1 performance passes at 5,108 scalar
  steps/s, 2.93M Blaze env-ticks/s, and 31.90 CUDA fps.
- R-02 direct lamp placement now uses `BlockRedstoneLight.onBlockAdded`, not
  the delayed `neighborChanged` path. Requested lit 124 without power settles
  to 123 immediately; requested unlit 123 with/without direct power settles
  to 124/123. Old magma fails only the lit-unpowered row at
  `trace/out/matrix_redstone_lamp_direct_add_probe_1/summary.md`. All three
  fixed states pass at
  `trace/out/matrix_redstone_lamp_direct_add_candidate_1/summary.md`, and 17
  lamp, lever, dust topology, and indirect strong-power cases pass at
  `trace/out/matrix_redstone_lamp_direct_add_affected_1/summary.md`. Native
  coverage passes at a 287 MB peak. GPU 1 performance passes at 5,114 scalar
  steps/s, 2.93M Blaze env-ticks/s, and 31.90 CUDA fps.
- R-02 direct lit-torch placement now runs the six-center
  `BlockRedstoneTorch.onBlockAdded` traversal before the outer placement
  notification. Normal supports forward represented strong power, so a newly
  added lit torch on a repeater-powered support queues its own +2 update after
  the two priority-ordered diode callbacks. Old magma fails the lit row while
  the unlit queue-free control passes at
  `trace/out/matrix_redstone_torch_direct_add_probe_2/summary.md`. Both fixed
  rows pass at
  `trace/out/matrix_redstone_torch_direct_add_candidate_1/summary.md`, and all
  16 placement, support-loss, saved-callback, strong-power, wall, and burnout
  cases pass at
  `trace/out/matrix_redstone_torch_direct_add_affected_1/summary.md`. Native
  coverage passes in 3:55 with a 288 MB peak and zero swap. GPU 1 performance
  passes at 5,165 scalar steps/s, 2.93M Blaze env-ticks/s, and 31.47 CUDA fps.
- R-02 saved floor-torch callbacks now admit ordinary normal supports whose
  captured power providers are all represented. A powered repeater beside the
  support turns a restored lit torch off at +2; valid stale lit and
  indirect-powered unlit callbacks drain without mutation. Old magma fails the
  two lit rows while the unlit control passes at
  `trace/out/matrix_redstone_torch_saved_indirect_probe_1/summary.md`. All
  three fixed rows pass at
  `trace/out/matrix_redstone_torch_saved_indirect_candidate_2/summary.md`, and
  the complete 19-case affected torch set passes at
  `trace/out/matrix_redstone_torch_saved_indirect_affected_1/summary.md`.
  Native coverage passes in 4:10 with a 289 MB peak and zero swap. GPU 1
  performance passes at 5,069 scalar steps/s, 2.93M Blaze env-ticks/s, and
  31.45 CUDA fps.
- R-02 saved wall-torch callbacks now restore both +2 inverter edges for
  metadata 1/2/3/4. The capsule resolves each attachment offset, validates its
  support and represented power providers, and preserves absolute queue
  fields. Disabling wall admission makes the EAST off/on pair fail at
  `trace/out/matrix_redstone_torch_wall_saved_probe_2/summary.md`. All eight
  directional cases pass at
  `trace/out/matrix_redstone_torch_wall_saved_candidate_2/summary.md`, and the
  complete 27-case torch family passes at
  `trace/out/matrix_redstone_torch_wall_saved_affected_1/summary.md`. Native
  coverage passes in 4:03 with a 291 MB peak and zero swap. GPU 1 performance
  passes at 5,109 scalar steps/s, 2.93M Blaze env-ticks/s, and 30.37 CUDA fps.
- R-02 saved torch callbacks now admit the promoted Forge directional
  side-solid surface: top slabs and stairs, eight-layer snow, hopper UP,
  farmland sides, and actual-shape stair sides. All six deliberate old-C rows
  omit their callbacks at
  `trace/out/matrix_redstone_torch_saved_directional_support_probe_1/summary.md`.
  The corrected focused set passes 6/6 at
  `trace/out/matrix_redstone_torch_saved_directional_support_candidate_3/summary.md`,
  and the affected torch family passes 33/33 at
  `trace/out/matrix_redstone_torch_saved_directional_support_affected_1/summary.md`.
  Java's hopper opacity zero is also restored, producing exact torch light 6
  in the support cell. Native coverage passes in 4:03 with a 291 MB peak and
  zero swap. GPU 1 performance passes at 5,095 scalar steps/s, 2.93M Blaze
  env-ticks/s, and 29.29 CUDA fps.
- R-02 saved floor-torch callbacks now cover every explicit
  `Block.canPlaceTorchOnTop` exception: all seven fence IDs, glass, stained
  glass, and cobblestone wall. Disabling only that branch makes all ten rows
  fail at
  `trace/out/matrix_redstone_torch_saved_top_exceptions_probe_1/summary.md`.
  The final exact sweep passes 10/10 at
  `trace/out/matrix_redstone_torch_saved_top_exceptions_candidate_3/summary.md`.
  It also corrected opacity for stained glass and fence IDs 188..192; the
  expanded 169-ID property table passes Java == CPU == CUDA. Native coverage
  passes with a 291 MB peak and zero swap. GPU 1 performance passes at 5,018
  scalar steps/s, 2.93M Blaze env-ticks/s, and 27.35 CUDA fps.
- W-01 also has an isolated natural `WorldServer.updateBlocks` proof. The
  parked fixture sanitizes all unrelated random-ticking blocks across the
  actual loaded collection and verifies exactly one eligible section/block.
  Because vanilla's player-chunk iterator is lazy and transient membership can
  change while earlier chunks tick, the fixture explicitly promotes the
  already-loaded target entry to recorded rank zero. The
  original capped-grass fixture was rejected after an aggregate run exposed a
  live-light-dependent no-op. The replacement uses a light-independent
  age-zero cactus callback and seed 75,682; acceptance requires rank zero and
  zero pre-selection ice/snow LCG advances. The focused run and aggregate
  select cactus and produce the sole age-0-to-age-1 mutation. General
  active-set persistence, multiple sections/speeds, grass spread, leaves, and
  fire remain active work.
- W-01 block-light addition now has a live, uncontaminated proof. A parked
  tick-0 command queues one real server-thread `setBlockState`, pre-light is
  sampled before it runs, and post-light is dumped as raw
  `EnumSkyBlock.BLOCK` bytes. Air-to-glowstone at `(12,79,8)` produces source
  15 and adjacent 14; magma matches all 10,625 cells exactly. Removing the
  same pre-existing source drains every cell back to zero exactly. The opacity
  case inserts stone beside the source, changing that cell 14-to-0 and
  rerouting the cell behind from 13 to 11 exactly. The comparator's identity
  and changed-value controls pass. A second source placed four blocks away
  changes its cell 11-to-15, the overlap edge 12-to-14, and exactly 1,304
  light cells with full Java/magma equality. A local-x15 source propagates
  directly into the next chunk's x=16 cell at light 14, with exactly 2,342
  changed cells.
- W-01 skylight now transfers the exact saved Java nibble field at the cold
  pre-tick boundary instead of reconstructing a converged approximation from
  blocks. A live opaque roof at `(12,86,8)` changes its own stored value
  15-to-0 and the eight air cells beneath it 15-to-14, with no unrelated
  saved-light churn. Removing the roof reverses those nine values exactly.
  The same placement at local x=15 proves the bounded update at a chunk
  boundary. General overhangs, translucent opacity ladders, simultaneous
  edits, and deferred gap relighting remain active W-01 coverage.
- S-01 drowning and submerged-to-dry air reset are complete. The 340-tick
  fixture reaches air -19 and matches two 2.5-health drowning hits plus hurt
  timer decay. The 80-tick surface fixture reaches air 252, resets to 300 on
  tick 38, and preserves full health.
- S-01 fire counters, wet extinguishing, and real fire-block contact are
  complete. The gates cover signed inactive fire state, 20-tick `ON_FIRE`
  damage, two `Entity.move` contacts, armorable `IN_FIRE` damage, 0.1
  exhaustion, ignition at counter zero, and water extinguishing.
- S-01 XP pickup is complete. A locked value-5 orb uses Java's actual entity
  ID and authoritative server state. Its hidden target-refresh cursor is
  explicitly seeded, so attraction does not depend on incidental entity ID
  modulo 100. Position, velocity, age, color, attraction, and removal match
  through tick 8, when both sides award exactly 5/7 of level zero. The runtime
  fix was Java's directional
  `playerBox.expand(1.0, 0.5, 1.0)` collection volume.
- S-01 melee is complete. The locked five-health NoAI target proves a partial
  attack, hurt-resistance rejection, four fully cooled one-damage hits, 0.1
  exhaustion only on accepted attacks, hurt/death timer decay, and removal at
  death time 20. Physical press edges and arm packets follow the integrated
  server's one-tick delay. Ordinary live mobs now age their hurt and resistance
  timers too; the broader mob suite catches the former infinite-immunity bug.
- S-01 potion duration is complete. A cold Speed II fixture starts at duration
  6, exposes authoritative durations 5 through 1, removes the effect on tick 5,
  and matches player position and motion through the derived speed-to-normal
  transition. The locked oracle mirrors the parked server's potion attribute
  state to the client before its next movement tick, eliminating Netty
  delivery jitter without granting an extra simulation tick. Magma applies the
  exact 1.11.2 operation-2 constants for Speed and Slowness. Periodic effects,
  brewing, particles, and the rest of the potion catalog remain separate
  feature work.
- The integrated runtime now carries a bounded server-player movement shadow.
  It consumes the preceding client's movement packet, retains server
  `onGround`, and routes jump/movement exhaustion through the delayed server
  path while preserving the current client pose and motion used for rendering.
  The 25-tick focused gate checks the exact `1,1,1,0` ground sequence and
  0.0548 exhaustion jump at the surface packet boundary.
- The Java gate parks the integrated server at a stable tick boundary, grants
  exactly one server tick per action, and samples authoritative vitals and raw
  blocks before releasing the next permit. The permitted server tick is
  required to finish before the client can emit the newly installed action's
  packet, eliminating a packet-order race found by repeated long-lived-client
  matrices. Key press queues and unsaved movement cursors are normalized at
  the parked pre-tick boundary.
- R-04 slime structure branching now ports the bounded 1.11.2 insertion and
  reorder rules for an EAST piston with a slime block and attached UP stone.
  Normal extension and sticky retraction match exact reverse move order,
  intermediate moving-tile states, settlement, sound RNG, and the shared
  12-block rejection boundary. Side obsidian is ignored as immovable. Simple
  terminal DESTROY cells are removed in reverse order before reverse movement
  on extension and sticky pull, with exact string drops, World/Math RNG, and
  entity-ID allocation. Canonical bed foot/head, all seven door lower/upper,
  deterministic or randomized double-plant lower/upper terminals, direct reed
  columns, and valid delayed cactus settlement columns now preserve their
  direct-versus-deferred ownership and ordered notification cascades. Native
  controls cover two terminals, all 56 canonical
  door lower states, all 28 upper states, all 40 deterministic double-plant
  pair states, randomized direct/deferred and mixed two-terminal rolls,
  pairing, bottom-up column order, and atomic rejection for direct, deferred,
  and settlement drops. The affected lifecycle family covers 88/88 behavior
  and raw outcomes at
  `trace/out/redstone_piston_slime_reed_cactus_column_family_1/summary.md`.
  Current composite promotion covers 385/385 behavior and raw outcomes through
  `trace/out/matrix_redstone_sticky_piston_retraction_boundary_full_1/summary.md`
  and the isolated scheduled-fire replacement; 381 rows are strict state
  passes and four delayed cactus trajectories remain explicit diagnostics
  after Java's controlled-input window.
- R-04 reed/cactus column coverage uses lower and middle cells on normal slime
  extension and sticky pull. Direct reeds reserve every still-present
  contiguous cell above the target and emit bottom-up during notification.
  Cactus uses the valid topology: the side stone begins diagonal, remains
  non-solid as block 36, and invalidates the column only when it settles.
  Native tests prove exact seeded order, middle-cell lower retention,
  three-stack capacity rejection, settlement timing, and capacity-limited
  sticky base-only retraction. A mixed test sums three direct reed and three
  delayed cactus drops before rejecting four free slots. The focused 16-case
  gate passes every behavior and raw outcome after the final combined-capacity
  fix at
  `trace/out/redstone_piston_slime_reed_cactus_column_final_1/summary.md`.
- R-04 sticky retraction now covers empty pull origins in all six facings,
  EAST immovable obsidian, serialized sticky and normal bases whose head is
  already absent, and a missing-head sticky base with an unrelated front
  stone plus immovable pull target. Java accepts the queued event without a
  head. Magma now does likewise, while clearing the front only for normal
  retraction, an admitted pull, or the matching settled head. Native controls
  prove start/settled metadata, exact contraction RNG, empty queues, and that
  unrelated blocks are preserved. The strict 20-case gate passes at
  `trace/out/redstone_sticky_piston_retraction_boundary_fix_2/summary.md`; the
  56-case retraction/slime-pull family passes every behavior and raw outcome at
  `trace/out/redstone_sticky_piston_retraction_boundary_family_1/summary.md`.
- R-04 one-stone sticky extension and pull now has start/settled Java proofs in
  all six facings. The existing bounded movement implementation was already
  direction-generic, so this slice required no production-code change. Native
  tests assert exact moving base/head/stone metadata, two-tile ownership,
  settlement, and sound RNG for every direction. The 28-case lifecycle family
  passes at
  `trace/out/redstone_sticky_piston_directional_stone_family_1/summary.md`, and
  the expanded 405-case aggregate passes at
  `trace/out/matrix_redstone_sticky_piston_directional_stone_full_1/summary.md`.
- R-04 sticky target reactions now cross five missing facings with birch
  planks, structure void, an empty chest, an extended normal piston, and an
  unextended normal piston. Together with the existing EAST obsidian control,
  these prove movable, DESTROY-but-not-pulled, tile, BLOCK, piston-special, and
  immovable outcomes. The pre-change 10-case probe passes exactly.
- R-04 repower during retraction exposed a real settlement bug. Java restores
  a moving piston base through `setBlockState`, runs `onBlockAdded`, sees power
  installed while the base was block 36, and re-extends. Magma now queues a
  bounded post-retirement power recheck for restored piston bases, avoiding a
  same-tile-tick progress advance. The deliberate two-cell failure is at
  `trace/out/redstone_sticky_piston_repower_during_retraction_probe_2/summary.md`;
  the focused fix and 65-case affected family pass at
  `trace/out/redstone_sticky_piston_repower_during_retraction_fix_1/summary.md`
  and
  `trace/out/redstone_sticky_piston_target_reaction_repower_family_1/summary.md`.
  The expanded 416-case aggregate passes at
  `trace/out/matrix_redstone_sticky_piston_repower_full_1/summary.md`.
- R-04 moving-piston checkpoint persistence is now exact. Java exposes every
  loaded `TileEntityPiston` in the proof volume, including moved block/meta,
  facing, source/extending flags, and the exact float bits of current and last
  progress. The neutral capsule validates and restores the same bounded
  64-entry active set in magma. A two-tile stone-push checkpoint resumes at
  progress 0.5, advances to 1.0, settles to the exact raw block volume, and
  passes the dedicated four-check gate at
  `trace/out/redstone_moving_piston_checkpoint_verify_final_1/summary.md`.
  Adding moving tiles to every state comparison exposed four pre-existing
  repower/remove cases that created a progress-zero tile one tick early. The
  restored-base check now enters a bounded next-tick queue and only when the
  settled base is still powered. The focused six-case timing family passes,
  the native runtime suite passes, and the final aggregate covers 421/421
  outcomes in 367.658 seconds at
  `trace/out/matrix_redstone_moving_piston_checkpoint_full_2/summary.md`, with
  417 strict state rows and the four existing cactus diagnostics. The narrow
  three-case oracle capture takes 20.230 seconds. GPU-1 guards pass unchanged
  floors at 2.93M Blaze env-ticks/s and 25.02 CUDA fps. Vanilla BlockEvents
  are transient live queues and are intentionally not claimed as disk-save
  persistence.
- R-04 static clipping now includes panes 101/102/160 and trapdoors 96/167.
  Pane center posts and arms follow the 1.11.2 glass/pane/full-cube,
  side-solid, snow, stair, slab, farmland, and redstone-block connection
  rules. Trapdoors use exact 3/16-thick top/bottom and four open orientation
  panels. The first Java divergence was not shape construction: moving items
  skipped the static-axis clipping already used by living mobs, so positive
  pane lanes fell through at tick 1 while empty-lane controls agreed. The
  generalized item path passes all eight pane/iron-bar cases at
  `trace/out/redstone_piston_pane_fix_1/summary.md`; all twelve trapdoor cases
  passed the first focused comparison. Native coverage locks all metadata,
  directions, connector discriminators, and empty lanes. The 482-case
  composite promotion and current performance figures are recorded in the
  baseline above.
- R-04 compound static clipping now includes cauldron 118 and hopper 154.
  Both use their exact base plus four 1/8-wide full-height rim boxes; the
  hopper base is 5/8 high and the cauldron base is 5/16 high. Ten item-lane
  cases cover each center and all four rims. Hopper fixtures are powered from
  below so item transfer is disabled and collision is measured independently
  of queued R-05 automation. Empty five-slot hopper tile state also round-trips
  through the neutral capsule on both sides. The full native runtime suite
  passes, and the 492-case composite promotion and current performance figures
  are recorded in the baseline above.
- R-04 directional static clipping now includes anvil 145, end rod 198, and
  dragon egg 122. Anvils use their facing-selected full-length axis and 1/8
  perpendicular insets across all three damage classes. End rods use the
  exact centered 1/4 cross-section for all six facings; downward item and
  horizontal pig probes distinguish all three axes and retain perpendicular
  and above-rod controls. Dragon eggs use the exact full-height 1/16 inset.
  The old-magma probe fails only the six occupied lanes while all six controls
  pass. The corrected 16-case family and full native suite pass. The clean
  508-case promotion and current performance figures are recorded above.
- Post-slice performance is within budget: scalar 4,105 steps/s, Blaze 2.91M
  env-ticks/s, and magma CUDA 26.26 fps against floors 3,858.9, 2.793M, and
  24.2915 respectively. Capsule, packed-block, and raw-light export are
  cold-only. Live skylight work is dirty-driven and bounded to vanilla's
  radius-17 update queue; unchanged frames do no added light work.
  The pending queue adds one count check when empty and scales with its bounded
  active set. Falling blocks use a fixed 16-entry pool and an empty-count fast
  path. Redstone adds six fixed neighbor callbacks only when a block edit
  occurs and scheduled lamp work only while pending. Flat dust uses a fixed
  work array only when an edit reaches a wire component; no heap allocation or
  full-world scan occurs in the tick loop. Torch work likewise runs only on a
  neighboring edit or while its callback is present in the pending queue.

## Test catalog to add

### State and survival

- `air_drowning_340`: implemented; submerged countdown, damage cadence, and reset.
- `surface_packet_boundary_25`: implemented; delayed packet, server landing, jump exhaustion.
- `fire_counter_45`: implemented; signed countdown and 20-tick burn damage.
- `fire_extinguish_5`: implemented; immediate water extinguish and no damage.
- `fire_contact_20`: implemented; contact damage, exhaustion, hurt immunity, and ignition.
- `xp_pickup_20`: implemented; locked entity ID/state, exact attraction,
  directional collection volume, removal tick, and level fraction.
- `melee_cooldown_70`: implemented; fixed target and physical-click tape,
  cooldown curve, immunity rejection, damage/exhaustion, hurt/death timers,
  and exact removal.
- `player_critical_locked`: implemented; 50 direct real-Java cases cover the
  complete represented critical predicate, raw target-health, motion, and fire,
  enchantment and armor ordering, every vanilla sword/axe/pick/shovel/hoe
  cooldown attribute, hit durability, ordinary grounded/air knockback, sprint
  and Knockback-enchantment impulses, attacker slowdown, sprint state, and
  Fire Aspect commit/rollback/lethal boundaries.
- `potion_speed_expiry_12`: implemented; fixed Speed II duration, expiry tick,
  exact movement attribute, and derived speed-to-normal travel.
- `potion_levitation_expiry_20`: implemented; exact rise, expiry-before-travel,
  fall-distance accumulation, and landing.
- `potion_jump_boost_30`: implemented for amplifier-aware client/server jump
  impulse, authoritative landing, exhaustion, and fall-damage reduction.
- `potion_water_breathing_expiry_8`: implemented; submerged air holds through
  the expiry tick and resumes decrement on the next tick.
- `potion_fire_resistance_expiry_25`: implemented for scheduled burn damage and
  fire-block contact; duration 1 protects its tick before effect aging, and the
  next unprotected scheduled hit is accepted.
- `potion_hunger_expiry_6`: implemented; Hunger II adds 0.01 exhaustion on all
  three active ticks, including the expiry tick, then stops.
- `potion_poison_cadence_4`: implemented; Poison II performs its duration-12
  hit, ages hurt time exactly, and cannot reduce health below one.
- `potion_regeneration_fire_recovery_5`: implemented; Regeneration I restores
  a scheduled burn on its duration-50 action tick without clearing hurt state.
- `potion_wither_cadence_4`: implemented; directly applied Wither II performs
  its duration-20 hit and exact hurt/duration aging.
- `potion_strength_melee_8`: implemented; Strength I raises a full-cooldown
  empty-hand hit from one point to four through the attack-damage attribute.
- `potion_weakness_melee_8`: implemented; Weakness I clamps empty-hand damage
  to zero while preserving swing cooldown and rejecting hurt/exhaustion.
- `potion_haste_mining_120`: implemented; Haste II raises mining speed and the
  attack-speed attribute, breaking staged stone within the focused window.
- `potion_mining_fatigue_180`: implemented; Mining Fatigue I lowers both
  attributes and retains the staged stone through the focused window.
- `potion_resistance_cactus_4`: implemented; Resistance I reduces one cactus
  contact point to 0.8 after armor while preserving hurt/exhaustion timing.
- `potion_absorption_cactus_12`: implemented; Absorption I consumes a gold
  heart without health/exhaustion loss, expires, then exposes the later hit.
- `potion_slowness_expiry_12`: implemented and now strict; Slowness II has five
  exact active rows, slower travel, and exact base-speed restoration.
- `potion_health_boost_expiry_5`: implemented; Health Boost II raises maximum
  health to 28, activates the natural-regeneration timer at health 20, and
  restores both cap and timer on the exact expiry tick.
- `potion_night_vision_render`: implemented; exact warning-flicker arithmetic,
  three dimension lightmaps, clear/terrain/fluid fog normalization, and a
  physics-clean real-Java sealed-tunnel pixel tape.
- `entity_idle`: implemented for a living NoAI pig through the melee fixture;
  exact ID, pose/motion/health, combat timers, spawn, and despawn.

### Raw world outcomes

- `water_source_dispatch_6`: implemented for the first scheduled source update,
  including four exact block mutations and the five-entry child queue.
- `water_flat_two_dispatches_8`: implemented; static source, level-1/2 rings,
  and exact 5-entry/12-entry pending queues.
- `water_downward_two_dispatches_8`: implemented; metadata-8 falling column,
  source wakeup, two level-1 rings, and exact 2-entry/10-entry queues.
- `lava_source_dispatch_31`: implemented; natural 30-tick cadence, four
  level-2 mutations, and exact five-entry child queue.
- `lava_flat_two_dispatches_61`: implemented; still source, level-2/4 rings,
  and exact 5-entry/12-entry pending queues.
- `lava_down_into_water_31`: implemented; enclosed water settles, downward
  contact creates stone, and the source requeues at +30.
- More complex `water_spread`, `lava_spread`, and reaction shapes.
- `falling_sand_land_20`: implemented; exact dispatch, nine airborne states,
  landing, +2 stability queue, and two raw mutations.
- `falling_sand_break`, gravel drop/flint RNG, anvils, dragon egg, dynamic
  fluid columns, and broader nonreplaceable landing cells. Metadata-0 gravel,
  one-cell water/lava/fire passthrough, shaped failed placement, timeout, and
  bounded free/X-wall lateral lifecycles are exact.
- `crop_random_tick`: callback implemented for wheat, including exact
  `java.util.Random` cursor consumption; full loaded-chunk selection remains.
- `cactus_random_selection`: isolated natural selector plus deterministic
  age-0-to-age-1 callback implemented with a zero-pre-advance RNG prefix.
- `grass_random_tick`: deterministic decay component exists, but the
  light-dependent live fixture was removed from the aggregate; spread and a
  separately controlled callback proof remain.
- `leaf_decay`.
- `fire_age_spread_extinguish`: exact for dry age/spread, disabled callbacks,
  Overworld/Nether netherrack and End-bedrock infinite sources, and both steady-rain rolls for
  source-exposed and covered-source/cardinal-neighbor-exposed age-15 stone
  fixtures. Effective thunder uses the same exact extinguish callback, and a
  direct fire callback ignites adjacent TNT with exact primed-entity cursors
  and motion. Broader humidity/material/precipitation topology remains.
- `block_light_add_remove`: implemented; real tick-0 glowstone placement and
  removal, exact source/neighbor propagation and drain, and 10,625-cell
  Java/magma light cuboids.
- `block_light_opacity`: implemented; opaque insertion, blocker attenuation,
  and rerouted field exact over 10,625 cells.
- `block_light_overlap`: implemented; two-source max-light merge and 1,304
  changed light cells exact.
- `block_light_chunk_edge`: implemented; local-x15 emitter, immediate
  next-chunk light 14, and 2,342 changed cells exact.
- `sky_light_column`.

Every raw-world test stores Java/C pre-state, post-state, transition CSV, tick
count, and an assertion that an expected mutation actually occurred.

### Redstone

- `redstone_lamp_on_1`: implemented; redstone-block placement, exact
  notification traversal, immediate 123-to-124 transition, and emitted light.
- `redstone_lamp_direct_add`: implemented for requested lit/unlit states with
  powered/unpowered inputs; exact immediate on-add normalization, no delayed
  placement callback, unchanged RNG/items, raw blocks, and light.
- `redstone_torch_direct_add`: implemented for lit/unlit floor states; exact
  Java on-add versus outer-notification ordering, indirect strong-power self
  callback, queue-free control, unchanged RNG/items, raw blocks, and light.
- `redstone_lamp_off_5`: implemented; source removal, exact +4 queue lifetime,
  124-to-123 callback, and complete light drain.
- `redstone_lamp_saved_powered_4`: implemented; capsule-restored absolute due
  time/order, exact dispatch boundary, and powered callback no-op.
- `redstone_lever_lamp_on_off`: implemented for a floor-mounted lever;
  metadata 5/13 weak power, immediate lamp-on, delayed lamp-off, transparent
  light propagation, and capsule-loaded powered callback.
- `redstone_lever_strong_power`: implemented through registry-normal stone and
  oak-plank supports. All six lever output directions work through stone;
  plank power-on/loss/save paths are exact, including the +4 lamp callback.
- `redstone_button_lamp_release_25`: implemented for a powered floor stone
  button; exact +20 release, +24 lamp-off handoff, save/load, and light drain.
- `redstone_button_all_orientations`: implemented for powered metadata 8..13
  across DOWN/EAST/WEST/SOUTH/NORTH/UP supports; every release preserves its
  low orientation bits, notifies the attached support, and hands the outward
  lamp an independent +4 callback.
- `redstone_wooden_button_arrow_occupied_32`: implemented with an exact
  stationary `EntityTippedArrow`; immediate 143:5-to-143:13 activation,
  lamp power, exact arrow state on every observation, and +30 callback
  replacement. Native negatives prove the same arrow does not activate stone.
- `redstone_wooden_button_release_7`: implemented from an arrow-free saved
  state; exact imminent +3 release, support notification, +4 lamp handoff,
  final light drain, and a capsule negative containing a captured arrow.
- `redstone_control_support_loss`: implemented for all eight lever metadata
  orientations and floor/wall/ceiling button attachments; exact normalized
  drops, powered-support notifications, stale button callbacks, Forge
  directional side solidity, unrelated-neighbor retention, and full-pool
  atomicity.
- `redstone_stone_pressure_plate_release_7`: implemented for a saved powered
  plate with no living entity in its trigger AABB; exact +3 release,
  support notification, direct/upward strong power, and +4 lamp handoff.
- `redstone_stone_pressure_plate_walkover_8`: implemented for a live forward
  crossing; null collision geometry, observation-3 activation, exact +20
  callback, lamp power, and complete block-light field.
- `redstone_stone_pressure_plate_occupied_22`: implemented for a player parked
  inside the trigger AABB through the due tick; exact queue lifetime, retained
  metadata 1, and another +20 callback.
- `redstone_stone_pressure_plate_mob_occupied_22`: implemented for a
  collision-enabled stationary pig; exact ordinary entity activation,
  stationary entity state, queue lifetime, retained metadata 1, and the next
  +20 callback. A true NoAI fixture is the unit-level non-activation control.
- `redstone_wooden_pressure_plate_item_occupied_22`: implemented for a
  stationary dropped item; exact EntityItem identity/stack/timers,
  observation-0 activation, queue lifetime, retained metadata 1, and the next
  +20 callback. An identical item over stone is the sensitivity negative.
- `redstone_pressure_plate_support_loss`: implemented for stone, wood, gold,
  and iron plates; exact normalized drops, powered-neighbor teardown, stale
  callbacks, stateful fully-opaque or fence support, and full-pool atomicity.
- `redstone_pressure_plate_direct_break`: implemented for powered stone, wood,
  gold, and iron states plus an unpowered negative; exact support-neighborhood
  notification, +4 lamp handoff, no item/RNG work, raw blocks, and light.
- `redstone_wire_direct_break`: implemented for powered and unpowered dust;
  exact six-center removed-block notification, indirect support-lamp +4
  handoff, unchanged entity/RNG state, raw blocks, and light.
- `redstone_wire_direct_add`: implemented for zero-power dust beside powered
  and unpowered repeaters; exact vertical and neighboring-wire on-add
  traversal, +2 repeater release, empty-queue control, unchanged RNG/items,
  raw blocks, and light.
- `redstone_diode_direct_break`: implemented for powered/unpowered repeaters
  and comparators; exact directional output teardown, comparator tile
  retirement, indirect lamp +4 handoff, unchanged RNG/items, raw blocks, and
  light.
- `redstone_repeater_direct_add`: implemented for powered and unpowered
  repeaters; exact directional on-add notification, same-tick indirect lamp
  activation, empty queue, unchanged RNG/items, raw blocks, and light.
- `redstone_comparator_direct_add`: implemented for aligned and rotated
  unpowered comparators; exact callback-before-tile ordering, output-zero tile
  creation, downstream repeater/lamp scheduling, empty-queue negative,
  unchanged RNG/items, raw blocks, and light.
- `redstone_tripwire_xp_occupied_1`: implemented for a moving XP orb; its
  post-move half-block box activates the middle segment, both hooks, and both
  lamps on observation 0. The exact stone-pressure-plate negative stays
  unpowered with no scheduled work.
- `redstone_tripwire_arrow_occupied_seed_0`: implemented with an exact
  stationary `EntityTippedArrow`; the middle segment, both hooks, and both
  lamps activate on observation 0 and follow the exact +10 lifecycle. The
  paired stone-pressure-plate case stays unpowered with no scheduled work.
- `redstone_tripwire_falling_sand_seed_0`: implemented for ordinary scheduled
  sand. The exact entity appears on observation 1, crosses and powers the
  attached line on observation 10, and retains exact trajectory, queue,
  entity state, six raw mutations, and block light through observation 11.
- `redstone_tripwire_live_break`: implemented for direct removal of attached
  string and a hook; exact immediate block lifecycle, +10 pulse, +14 lamp
  handoff, empty-start queue, detach-sound RNG, no item, raw blocks, and light.
- `redstone_tripwire_on_add`: implemented for completing an opposite-facing
  hook line and isolated placement; exact attach metadata, SOUTH/WEST scan,
  +10 callback lifetime/drain, unchanged RNG, raw blocks, and light.
- `redstone_tripwire_shears_break_disarm`: implemented through the real
  survival harvest lifecycle; exact DISARMED-before-break ordering, hook
  detach without an alarm pulse, string drop, shears wear, exhaustion,
  scheduled work, RNG/EID cursors, raw blocks, and light. Physical CPU and
  CUDA mining share the same harvest cost and tool marker.
- `redstone_piston_diode_shape`: implemented for unpowered/powered repeaters
  and comparators. Java fixtures lock the inherited full-footprint 1/8
  collision surface during a downward piston push; native tests exhaust all
  64 ID/metadata combinations, and CPU/CUDA selection uses the same box.
- `redstone_weighted_pressure_plate_two_entities_12`: implemented for gold
  and iron. Player plus one dropped-item entity produces analog strengths 2
  and 1, propagates the same metadata into dust, powers the lamp, and replaces
  the due callback at +10. A stack-count-64 native negative remains strength 1.
- `redstone_light_weighted_pressure_plate_release_7`: implemented from a
  capsule-restored gold plate at strength 2; exact +3 release, dust 2-to-0,
  support notification, +4 lamp handoff, and final raw-light drain.
- `redstone_wire_component`: implemented for one-wire lamp on/off, metadata 15
  through 1 in a 15-wire line, the zero-power 16th-wire cutoff, a T-branch,
  closed-loop source removal, and one-block registry-normal climb/descent
  edges. Stone and oak-plank climb fixtures are exact. The long line observes
  every intermediate distance, including the requested length-14 state.
- `redstone_wire_strong_power`: implemented through an adjacent stone support.
  Power-on is immediate; source removal drains the wire without self-power and
  hands the indirectly powered lamp its exact +4 delayed-off callback.
- Other support/obstruction materials, multi-level mixed-component update
  order, and components beyond the bounded 256-wire proof.
- `redstone_torch_floor_inverter`: implemented for live and capsule-restored
  power-off/power-on edges, exact +2 queues, metadata 5, and emitted-light
  addition/drain.
- `redstone_torch_saved_indirect_callback`: implemented for represented strong
  power through a normal support plus stale lit and powered-unlit controls;
  exact absolute queue drain, toggle history, raw blocks, and block light.
- `redstone_torch_floor_burnout`: implemented with a reusable per-tick edit
  sequence, rolling 60-tick history, eighth-toggle burnout, exact burnout RNG
  consumption, +160 recovery deduplication, pruning, relight, and exact light.
- `redstone_torch_wall_inverter`: implemented for metadata 1/2/3/4 in both
  live and capsule-restored edge directions with exact +2 callbacks, absolute
  queue fields, raw blocks, and block light.
- `redstone_torch_strong_power`: implemented for a lit torch below stone and
  an on-add lamp adjacent only to that support.
- `redstone_torch_checkpoint_history`: implemented; the Java bridge captures
  the chronological hidden toggle list, the capsule restores it, and
  `trace/run_torch_checkpoint_regression.sh` proves a seven-toggle checkpoint
  followed by an exact eighth-toggle continuation.
- `redstone_torch_support_loss`: implemented for floor and wall attachments,
  exact item 76:0 spawn/RNG/entity cursors, lit second-ring notifications,
  Forge directional side solidity, and bounded full-item-pool rejection.
- Repeater delays 1-4, side lock, update priority.
- Comparator compare/subtract and container fullness.
- Observer one-tick pulse and update suppression.
- Normal/sticky piston extension/retraction, 12-block limit, immovable blocks,
  slime adjacency, entity collision, and save/reload mid-motion.
- Hopper transfer/lock, dropper/dispenser item-specific behavior.
- Powered/detector/activator rail motion and minecart variants.

Each case compares packed block state after every tick, pending scheduled work,
tile/container state, entity motion, inventory deltas, and pixels at stable
checkpoints.

### Generation and feature bundles

For each generator, choose at least three positive seeds and one negative
region, then compare locate result, bounding boxes, packed block volume,
tile/entity payloads, and loot. Runtime bundles additionally require an
`off`-mode test proving no state allocation, ticking, rendering, or RNG
consumption.

## Performance contract

Feature work may not buy fidelity by scanning the whole loaded world every
tick. Use allocate-once pools, active sets, scheduled/dirty queues, spatial
indices, and shared CPU/CUDA code. No heap allocation is allowed in the
simulation or render hot loops.

The machine-local guard is:

```bash
cd c/magma
uv run --no-project python trace/perf_guard.py --only all --gpu 1
```

Current medians on GPU 1:

| Metric | Baseline | Regression floor |
|---|---:|---:|
| mc-sim scalar tick kernel | 4,062 steps/s | 3,858.9 |
| blaze full-feature t0, N=8192 | 2.94M env-ticks/s | 2.793M |
| magma CUDA, 1920x1080, view distance 8 | 25.57 fps | 24.2915 fps |

The 5% floors catch meaningful regressions while tolerating shared-machine
noise. Any failure blocks the change until it is optimized or the user
explicitly accepts a measured tradeoff. These are regression floors, not ship
targets: the existing 60 fps renderer target remains open.

The first straight-stone/12-block-limit sample is preserved at
`trace/out/perf_guard_redstone_piston_stone_line_limit.json`
(3,076 scalar steps/s, 2.92M Blaze env-ticks/s, 22.48 CUDA fps). It was taken
at host load 75.98 with an unrelated 60-thread self-play process consuming
roughly 60 cores; CPU and the CPU-fed render loop failed while the independent
Blaze GPU metric passed. This does not promote the slice: the identical
ordinary-affinity guard must pass after contention retires.

Earlier reaction slices had an unresolved performance gate. Their
ordinary-affinity sample at
`trace/out/perf_guard_redstone_piston_block_reactions.json`
measured 3,236 scalar steps/s, 2.92M Blaze env-ticks/s, and 22.31 CUDA fps
under unrelated host work. A second diagnostic pinned to the otherwise
quieter CPU set 0-59 (excluding busy CPU 18) and GPU 1 is preserved at
`trace/out/perf_guard_redstone_piston_block_reactions_isolated_0_59_1.json`;
it measured 3,288 scalar steps/s, 2.93M Blaze env-ticks/s, and 23.67 CUDA fps.
Blaze passed both times, while CPU and the CPU-fed renderer remained below
their floors. No failure is accepted as a new baseline. The added live
collision path executes only when both fixed piston and item active sets are
nonempty, with no allocation or loaded-world scan.
The metadata-preserving allium, orientation-stripping torch, block-to-item
wire, zero-drop fire, and suppressed-snow extensions add only constant
payload predicates to that already-active DESTROY path and no idle work. The
  fire branch returns before drop-RNG/entity-capacity work, then the common
  successful-extension path consumes its one pitch draw; snow performs at
  most nine drop-chance draws plus that pitch draw only after an active piston
  reaches that state. The latest
192-case correctness aggregate completed at host load 79.77 while an unrelated
44-thread self-play process consumed roughly 44 cores and several
training/search jobs remained active. GPU 1 was idle, but the scalar and
CPU-fed renderer metrics were still contaminated, so no additional
throughput sample from that window is considered valid promotion evidence.
After load fell to 28.68 on the 192-CPU host, the unchanged full guard at
`trace/out/perf_guard_redstone_piston_mushroom_destroy_1.json` passed all
frozen floors: 4,319 scalar steps/s versus 3,858.9, 2.93M Blaze env-ticks/s
versus 2.793M, and 26.71 1080p CUDA fps versus 24.2915. Mushroom support adds
two constant predicates only inside the already-active DESTROY path. The
194-case slice was therefore correctness- and performance-promoted without a
threshold or baseline change. The attached-ladder extension adds one constant
metadata-normalization predicate inside that same active path, while the
Java-only oracle correction has no magma hot-path cost. At host load 27.68
with GPU 1 idle, the unchanged guard at
`trace/out/perf_guard_redstone_piston_ladder_destroy_1.json` also passed:
4,158 scalar steps/s, 2.92M Blaze env-ticks/s, and 27.12 1080p CUDA fps. The
195-case slice was promoted with the same frozen baseline and floors. Cobweb
support adds one constant block-to-item predicate inside the already-active
DESTROY path and no idle work. At host load 29.40 with GPU 1 idle, the
unchanged guard at
`trace/out/perf_guard_redstone_piston_cobweb_destroy_1.json` passed all
floors: 4,149 scalar steps/s, 2.91M Blaze env-ticks/s, and 26.24 1080p CUDA
fps. The 196-case slice is promoted without changing a baseline or threshold.
The ordinary/lit pumpkin pair adds two constant ID predicates within the
active payload lookup and no idle work. At host load 22.50 with GPU 1 idle,
the unchanged guard at
`trace/out/perf_guard_redstone_piston_pumpkin_destroy_1.json` passed:
4,199 scalar steps/s, 2.91M Blaze env-ticks/s, and 26.47 1080p CUDA fps. The
198-case slice is promoted against the same baseline and floors.
Structure-void support adds one constant no-items predicate inside the active
payload lookup and no idle work. At host load 44.12 with GPU 1 idle, the
unchanged guard at
`trace/out/perf_guard_redstone_piston_structure_void_destroy_1.json` passed:
4,287 scalar steps/s, 2.92M Blaze env-ticks/s, and 25.12 1080p CUDA fps. The
renderer result is closer to its 24.2915 floor than the previous low-load
samples but remains a valid pass; no baseline or threshold changed.
The redstone-control candidate adds explicit payload mappings and bounded
break-notification work only while an active piston destroys one of those
blocks. Its item SELF collision arrays and represented-shape scan are entered
only when the fixed entity pool has active rows; the no-item path retains the
original player-tick call without array initialization or a world scan. Three
unchanged-guard attempts are preserved at
`trace/out/perf_guard_redstone_piston_control_destroy_1.json`,
`trace/out/perf_guard_redstone_piston_control_destroy_2.json`, and
`trace/out/perf_guard_redstone_piston_control_destroy_3_pinned.json`.
Blaze passed at 2.91-2.93M env-ticks/s, but scalar CPU measured
2,937/3,251/3,126 against its 3,858.9 floor and CUDA measured
21.57/23.22/22.92 against its 24.2915 floor while an unrelated 64-core
self-play process drove host load to roughly 95. The three measured binaries
predate this slice (2026-07-29), including the independent scalar benchmark,
so the scalar miss is an environmental control rather than evidence that a
new runtime branch caused the slowdown. These failed samples are not
promotion evidence, thresholds remain frozen, and the later quiet-host pass
is recorded below.

Latest R-04 GPU guards pass in
`trace/out/perf_guard_redstone_piston_minimum_pulse_blaze_1.json` and
`trace/out/perf_guard_redstone_piston_minimum_pulse_cuda_1.json` at 2.93M Blaze
env-ticks/s and 28.56 CUDA fps. The current scalar sample on an idle physical
pair measured 3,650 steps/s against the unchanged 3,858.9 floor while host
load remained elevated. The non-pass remains in
`trace/out/perf_guard_redstone_sticky_piston_cpu_physical_idle_1.json`; no
threshold changed. The preceding quiet-host scalar confirmation in
`trace/out/perf_guard_redstone_piston_comparator_destroy_cpu_affinity_1.json`
passes at 4,122 steps/s. Beetroot adds only an event-local payload branch that
the scalar steady-state workload does not enter. Retraction likewise starts
only from an edited redstone neighbor. Minimum-pulse clearing and sticky
movement reuse the existing bounded piston tile tick. A quiet-host
current-source scalar rerun remains open bookkeeping.
The tagged-item, shulker,
skull, and flower-pot pools are allocate-on-use and add no loaded-world or
per-tick scan. It uses
the exact current runtime, current scalar benchmark, and native-sm_120 CUDA
renderer. Tripwire collision work is bounded to represented entity movement,
and its rechecks use the existing scheduled-tick queue; it adds no idle world
scan or allocation. The dragon-egg, cocoa, vine, waterlily, wart, stem, melon,
and cake DESTROY paths are reached only during active piston work and add no
idle scan or allocation. Carrot/potato/beetroot drop selection and comparator
tile retirement are likewise confined to an already-active piston DESTROY
path and add no idle work or allocation.
The preceding stem guard is `trace/out/perf_guard_redstone_piston_stem_1.json`
(5,103 scalar steps/s, 2.94M Blaze env-ticks/s, 31.55 CUDA fps). The preceding melon
guard is `trace/out/perf_guard_redstone_piston_melon_1.json`
(4,948 scalar steps/s, 2.93M Blaze env-ticks/s, 31.13 CUDA fps). The preceding
cake guard is
`trace/out/perf_guard_redstone_piston_cake_idle_1.json`
(5,031 scalar steps/s, 2.93M Blaze env-ticks/s, 31.47 CUDA fps). The preceding
fluid guard remains at
`trace/out/perf_guard_redstone_piston_fluid_block_idle_1.json`
(4,929 scalar steps/s, 2.93M Blaze env-ticks/s, 31.66 CUDA fps). The preceding
shulker guard remains at
`trace/out/perf_guard_redstone_piston_shulker_nbt_idle_1.json`
(4,983 scalar steps/s, 2.93M Blaze env-ticks/s, 31.15 CUDA fps). The preceding
plain-shulker guard remains at
`trace/out/perf_guard_redstone_piston_shulker_box_idle_1.json`
(4,490 scalar steps/s, 2.93M Blaze env-ticks/s, 29.04 CUDA fps). The preceding
ownerless-skull guard remains at
`trace/out/perf_guard_redstone_piston_skull_idle_1.json`
(4,369 scalar steps/s, 2.93M Blaze env-ticks/s, 29.91 CUDA fps). The preceding
flower-pot guard remains at
`trace/out/perf_guard_redstone_piston_flower_pot_idle_1.json`
(4,162 scalar steps/s, 2.93M Blaze env-ticks/s, 28.81 CUDA fps). The preceding
door guard remains at
`trace/out/perf_guard_redstone_piston_door_idle_1.json`
(5,080 scalar steps/s, 2.93M Blaze env-ticks/s, 31.26 CUDA fps). The preceding
bed guard remains at
`trace/out/perf_guard_redstone_piston_bed_idle_1.json`
(5,066 scalar steps/s, 2.93M Blaze env-ticks/s, 31.13 CUDA fps). The
preceding chorus guard remains at
`trace/out/perf_guard_redstone_piston_chorus_idle_1.json`
(5,036 scalar steps/s, 2.93M Blaze env-ticks/s, 31.27 CUDA fps). The
preceding cactus guard remains at
`trace/out/perf_guard_redstone_piston_cactus_idle_1.json`
(4,209 scalar steps/s, 2.91M Blaze env-ticks/s, 26.55 CUDA fps). The preceding
reed-column guard remains at
`trace/out/perf_guard_redstone_piston_reed_column_1.json`
(4,105 scalar steps/s, 2.91M Blaze env-ticks/s, 26.26 CUDA fps). The preceding
tall-grass guard remains at
`trace/out/perf_guard_redstone_piston_tall_grass_1.json`
(4,266 scalar steps/s, 2.92M Blaze env-ticks/s, 24.67 CUDA fps). The preceding
dual-piston sound-RNG guard remains at
`trace/out/perf_guard_redstone_piston_dual_sound_rng_1.json`
(4,222 scalar steps/s, 2.92M Blaze env-ticks/s, 26.25 CUDA fps). The preceding
clean structure-void guard
remains at `trace/out/perf_guard_redstone_piston_structure_void_destroy_1.json`
(4,287 scalar steps/s, 2.92M Blaze env-ticks/s, 25.12 CUDA fps). The preceding
clean pumpkin guard remains at
`trace/out/perf_guard_redstone_piston_pumpkin_destroy_1.json`
(4,199 scalar steps/s, 2.91M Blaze env-ticks/s, 26.47 CUDA fps). The earlier
clean cobweb guard remains at
`trace/out/perf_guard_redstone_piston_cobweb_destroy_1.json`
(4,149 scalar steps/s, 2.91M Blaze env-ticks/s, 26.24 CUDA fps). The earlier
clean ladder guard remains at
`trace/out/perf_guard_redstone_piston_ladder_destroy_1.json`
(4,158 scalar steps/s, 2.92M Blaze env-ticks/s, 27.12 CUDA fps). The earlier
clean mushroom guard remains at
`trace/out/perf_guard_redstone_piston_mushroom_destroy_1.json`
(4,319 scalar steps/s, 2.93M Blaze env-ticks/s, 26.71 CUDA fps). The earlier
clean single-stone guard remains at
`trace/out/perf_guard_redstone_piston_single_stone_push_rerun_1.json`
(4,163 scalar steps/s, 2.91M Blaze env-ticks/s, 25.54 CUDA fps).
Saturating unrelated host work contaminated the preceding
quasi-only and combined samples, which remain preserved at
`trace/out/perf_guard_redstone_piston_quasi_connectivity.json`
(3,156 scalar steps/s, 2.92M Blaze env-ticks/s, 21.98 CUDA fps) and
`trace/out/perf_guard_redstone_piston_single_stone_push.json`
(3,628 scalar steps/s, 2.92M Blaze env-ticks/s, 23.63 CUDA fps). The exact
ordinary-affinity command passed with no code change after the 64-thread
self-play and transient NNUE/CUDA-toolchain workers retired. The preceding
directional-wire/indirect-cube guard remains at
`trace/out/perf_guard_redstone_piston_directional_wire_indirect.json`
(4,006 scalar steps/s, 2.91M Blaze env-ticks/s, 25.62 CUDA fps). The preceding
directional-observer guard remains at
`trace/out/perf_guard_redstone_piston_directional_observer_rerun_2.json`
(4,221 scalar steps/s, 2.92M Blaze env-ticks/s, 25.56 CUDA fps). Two earlier
observer samples during a transient unrelated
63-core training workload are preserved rather than promoted at
`trace/out/perf_guard_redstone_piston_directional_observer.json` and
`trace/out/perf_guard_redstone_piston_directional_observer_rerun_1.json`:
CPU measured 3,257/3,338 steps/s and CUDA measured 23.33/23.45 fps, while
Blaze passed at 2.92M/2.93M. The same ordinary-affinity command passed with
no code change after that workload exited. The preceding
directional-comparator guard remains at
`trace/out/perf_guard_redstone_piston_directional_comparator.json`
(4,163 scalar steps/s, 2.91M Blaze env-ticks/s, 26.04 CUDA fps). The preceding
directional-repeater guard is at
`trace/out/perf_guard_redstone_piston_directional_repeater_rerun_1.json`
(4,278 scalar steps/s, 2.91M Blaze env-ticks/s, 25.96 CUDA fps). Its first
ordinary-affinity sample under concurrent unrelated host CPU work is
preserved at
`trace/out/perf_guard_redstone_piston_directional_repeater.json`: Blaze and
CUDA passed at 2.90M and 25.37 fps, but scalar CPU measured 3,827 steps/s
against the 3,858.9 floor. The immediate full rerun above passed without a
code or affinity change. The preceding directional-torch guard remains at
`trace/out/perf_guard_redstone_piston_directional_torch.json`
(4,253 scalar steps/s, 2.91M Blaze env-ticks/s, 26.49 CUDA fps). The preceding
direct-power-source guard remains at
`trace/out/perf_guard_redstone_piston_direct_power_sources.json`
(4,138 scalar steps/s, 2.91M Blaze env-ticks/s, 25.82 CUDA fps). The preceding
direct-control clean guard remains at
`trace/out/perf_guard_redstone_piston_direct_controls_clean_1.json`
(3,960 scalar steps/s, 2.92M Blaze env-ticks/s, 25.43 CUDA fps). Its first
full sample under concurrent 64-thread TAK work is
preserved at `trace/out/perf_guard_redstone_piston_direct_controls.json`;
CPU/Blaze passed but CUDA fell to 21.38 fps. A 40-core diagnostic affinity
sample fell to 20.33 fps at
`trace/out/perf_guard_redstone_piston_direct_controls_cuda_idle_set_1.json`,
and an ordinary-affinity retry remained at 20.80 fps while contention
continued. After that workload retired, the isolated renderer passed at
25.53 fps in
`trace/out/perf_guard_redstone_piston_direct_controls_cuda_rerun_2.json`,
followed by the all-green full guard above. The preceding powered-lever guard
is preserved at `trace/out/perf_guard_redstone_piston_powered_lever.json`
(4,280 scalar steps/s, 2.91M Blaze env-ticks/s, 25.96 CUDA fps). The earlier
six-facing coverage guard is preserved at
`trace/out/perf_guard_redstone_piston_empty_extension_six_faces_clean_2.json`
(4,087 scalar steps/s, 2.92M Blaze env-ticks/s, 25.07 CUDA fps). Its first
post-154-matrix sample is preserved at
`trace/out/perf_guard_redstone_piston_empty_extension_six_faces.json`: under
heavy unrelated host load, CPU fell to 3,677 steps/s and CUDA to 22.96 fps
while Blaze still passed. A diagnostic one-core-affinity run at
`trace/out/perf_guard_redstone_piston_empty_extension_six_faces_clean_1.json`
then passed CPU and Blaze but held CUDA to 24.20 fps, demonstrating that the
affinity itself was unsuitable for renderer promotion. The ordinary-affinity
full rerun above is the promotion evidence. The earlier first-slice
post-matrix sample is preserved at
`trace/out/perf_guard_redstone_piston_empty_extension.json`: CPU and Blaze
passed, but two cold/noisy CUDA samples lowered its median to 23.90 fps. An
immediate isolated CUDA rerun passed at 25.79 fps in
`trace/out/perf_guard_redstone_piston_empty_extension_cuda_rerun_1.json`,
followed by an all-green 4,130/2.92M/25.68 full guard at
`trace/out/perf_guard_redstone_piston_empty_extension_clean_1.json`. The
fixed 64-entry piston table is
visited only when `piston_count` is nonzero; the inactive path is one branch
with no scan or allocation. A separate earlier four-oracle-pool-loaded
measurement at
`trace/out/perf_guard_redstone_comparator_item_frame_oracle_pool_loaded.json`
was preserved as contaminated load evidence rather than used for promotion;
the current one-oracle measurement above is the comparable guard. Observer
work is triggered only by represented block changes and scheduled callbacks
and checks at most the six adjacent cells; it adds no loaded-world scan,
per-tick observer pass, or hot-loop allocation. Comparator
tile state uses a fixed 64-entry pool touched only on load, edit, query, or
callback. Single-chest fullness is computed only when a comparator callback
or explicit edit reaches that exact source; the normal no-chest hot path adds
no loaded-world scan or allocation. Furnace comparator state reuses the
existing fixed 16-entry tile pool and its already-active tick loop; no new
inactive scan or allocation was added. Double-chest pairing performs four
block checks and resolves the represented adjacent tile only on a comparator
query; it adds no per-tick work. Closed single/double trapped-chest inventory
state reuses the same bounded chest path and is distinguished by block ID only
at edit, load, and query boundaries; it adds no per-tick world scan. Live
viewer power performs a bounded fixed-pool tile lookup only when an existing
redstone query has already reached block ID 146. Open/close notifications are
input-driven and touch the chest and block below; the idle path adds no
loaded-world scan or heap allocation. Dispenser/dropper state uses a cold
growable pool capped at 256 represented tiles. The pool is absent until a
saved tile is restored, has no tick hook, and is queried only after a
comparator reaches the exact block coordinate; the fullness loop is bounded
to nine slots. Jukeboxes share that cold coordinate lookup and read one record
slot only; they add no tick hook, playback work, or audio system. The
command-block comparator subset likewise uses a cold pool capped at 256
represented tiles. It is absent until exact saved state is restored, is
queried only after a comparator reaches the exact coordinate, and has no
command-execution or per-tick path. Item frames use a separate cold pool
capped at 256 exact represented frames. It is absent until capsule restore,
has no tick hook, and is inspected only by the comparator's bounded rear-cell
proof; support or hanging-cell edits retire affected state immediately. The
fixed-capacity 3D
component graph is constructed only when an edit reaches dust; registry
lookups, indirect queries, and the torch's fixed 6-by-6 notification traversal
are edit/callback driven. Live pressure-plate collision work visits the server
player’s contracted bounding-box cells and, after an enabled mob pass, scans
the fixed 95-slot living pool once and visits only each represented mover's
contracted cells. Item collisions visit the fixed 48-slot item pool only when
at least one item is active, then inspect only each item's contracted cells.
Trigger occupancy remains a bounded active-pool query. No loaded-world scan
or hot-loop heap allocation was added. Weighted strength is an integer count
over those same represented sets and does not add a second occupancy pass.
Wooden-button occupancy scans only the pre-existing fixed 32-projectile pool
when an arrow collides with a button or its callback is due. Arrow tripwire
and plate activation traverses only the fixed cells crossed by that active
projectile and reuses the same pool for bounded occupancy queries. Falling
block collision work is entered only when `falling_block_count` is nonzero;
it traverses the active entity's contracted cells and reuses the fixed
16-entry store for occupancy, without an idle scan or allocation.

For a new optional bundle:

- Disabled: median base-profile performance must stay within noise; no
  per-tick full-world branch or memory sweep.
- Enabled: capture a separate before/after profile and set a feature-specific
  budget before implementation. Cost must scale with active objects, not total
  world capacity.
- CUDA: run byte/bit parity before interpreting throughput.

## Aggregate gates after every fix

```bash
# Narrow test for the changed subsystem first.

cd c/magma
uv run --no-project python trace/run_oracle_matrix.py --instances 2
bash game/test_player_ctl.sh
uv run --no-project python trace/perf_guard.py --only all --gpu 1

cd ../..
bash netherite_sweep.sh --quick
```

Run the relevant pixel family and `netherite_sweep.sh --full --gpu 1` before a
milestone or when shared CPU/CUDA/render code changes.

## 2026-08-06 20% checkpoint

The effort-weighted full-replica estimate is now approximately 20%, with high
uncertainty. This is not a raw feature-count percentage. It advances the prior
16% checkpoint for three measured product slices:

- G-01 is `DONE`: normal and Mesa abandoned-mineshaft topology, placement,
  rails, webs, fences, chest-minecart loot, and cave-spider spawners are live,
  alongside the existing exact dungeon path. The permanent parity gate covers
  807 pieces, 138,240 placement states, and 868 loot fields.
- F-02 is `ACTIVE`: a table can be opened and used in the product with exact
  bookshelf scan, seeded offers, inventory/lapis movement, costs, application,
  reseeding, and book conversion. The original 6,804-field kernel gate and an
  eight-case real `ContainerEnchantment` gate pass.
- F-01 is `ACTIVE`: exact timer/strength state passes Java, CPU, and CUDA over
  2,560 fields; rain/snow geometry passes its 24-vertex Java lock; an exposed
  player is extinguished by rain while the clear control keeps burning; and a
  native 320x180 probe contains visible precipitation rather than only global
  color change. Java weather pixel tapes and the remaining weather behaviors
  listed in F-01 stay open.

The disabled base profile remains allocation-free for these optional paths and
passes the fresh scalar performance guard at 4,844 steps/s against the 3,858.9
floor (`trace/out/perf_guard_20pct_checkpoint_cpu.json`).
Active 320x180 rain rendered 120 frames in 1.49 seconds with 109,484 KiB peak
RSS; its clear control took 1.06 seconds and 105,120 KiB. These active-feature
figures are diagnostic budgets, not a claim of weather pixel parity. The
checkpoint `netherite_sweep.sh --quick` run passed all 14 steps with no skips.

## 2026-08-06 30% checkpoint

The effort-weighted full-replica estimate is now approximately 30%, with high
uncertainty. It is still not a raw feature-count or test-count percentage. The
portfolio is 15 `DONE`, 12 `ACTIVE`, and five `QUEUED` bundles. The increase
from 20% prices the implemented portions of four previously large gaps:

- F-01 remains `ACTIVE`, but its gameplay state now includes the exact
  lightning lifecycle, spawn RNG, sound/event emission, bounded fire
  placement, represented entity strikes and mob conversion, plus weather
  ice, snow, and cauldron callbacks. Weather visuals/audio playback and the
  broader precipitation/pixel edges in the queue remain open.
- E-02 is `DONE` under its narrow acceptance: ship elytra acquisition,
  recipes, Java-locked rocket constructor and first motion, elytra boost,
  explosion damage, and durability all pass. Cosmetic particle fidelity is
  owned by V-01 rather than hidden in this completed gameplay bundle.
- L-01 is `ACTIVE`: casting/bobber state, water and block collision, exact
  catch timers, open-water/weather/luck/lure modifiers, reeling, and the real
  nested 1.11.2 fish/junk/treasure table are implemented. The real
  `LootTableManager` comparison covers 384 output fields including random
  damage and level-30 enchantment application. The fishing line and remaining
  entity/persistence/pixel edges stay open.
- E-01 is `ACTIVE`: existing outer-island terrain now has exact chorus,
  island, and gateway population primitives; live discovery populates and
  caches a bounded 5x5 chunk neighborhood. Four real
  `StructureEndCityPieces` graphs match, real templates build cities and
  ships, gateway travel is live, the ship elytra is collectible, and the
  exact End-city treasure comparison covers 868 fields. Shulkers, arbitrary
  chunk-load-order population persistence, chorus internal-face pixel
  culling, and broader End pixel/state-capsule work remain open.

The focused promotion gates pass for weather world/lightning, fireworks,
fishing timers and real nested loot, End gateways, End-city graph/runtime/
loot, and End population/runtime. The final CPU/CUDA Blaze comparison is
bitwise over 64,000 environment ticks for camera, depth, edge, pose, done,
reward, and scalar outputs. During performance promotion it caught a real
snapshot regression: the newly effect-aware `PvStats.maxHealth` was not in the
legacy `.bsnp` schema and reset left it at zero, causing lethal clamps and
large periodic masked resets. Reset now restores the vanilla 20 HP base.

The clean three-metric guard passes at 4,369 CPU scalar steps/s, 2.87M Blaze
CUDA environment ticks/s, and 26.76 1080p CUDA frames/s. Their floors are
3,858.9, 2.793M, and 24.2915 respectively; evidence is
`trace/out/perf_guard_30pct_checkpoint.json`. Optional End population is
discovery-cached, and none of these bundles adds an idle full-world sweep or
hot-loop allocation to the base profile. The final
`netherite_sweep.sh --quick` run passes all 14 steps with no skips.

## 2026-08-07 40% checkpoint

The effort-weighted full-replica estimate is now approximately 40%, with high
uncertainty. It is not a raw feature-count or test-count percentage. The
portfolio is 15 `DONE`, 14 `ACTIVE`, and three `QUEUED` bundles. The increase
from 30% prices substantial, tested portions of three large active bundles:

- R-05 moved from `QUEUED` to `ACTIVE`. Live five-slot hoppers push, pull,
  capture items, chain, respect redstone locking, and use the exact cooldown.
  Droppers insert, default dispensers eject, and arrow dispensers create
  projectiles. Rideable, TNT, and hopper minecarts now follow straight and
  sloped rails; powered, detector, and activator rails have live behavior.
- G-03 moved from `QUEUED` to `ACTIVE`. Exact four-facing desert pyramids,
  jungle temples, and swamp huts are part of live population. Their traps,
  puzzle, persistent chest/dispenser seeds, exact loot, generated dispenser
  realization, swamp empty-pot tile quirk, and witch site are retained.
  Monuments, mansions, igloos, runtime witch lifecycle, and complete generated
  block rendering remain open.
- L-01 remains `ACTIVE`, but fishing now has a complete cold hook-state restore
  boundary, block/item/living interception, retract and pull behavior, rod
  wear, XP and item emission, and exact nested loot in addition to its existing
  cast and catch-timer coverage.

The focused Java-to-native gates pass for automation, minecarts, fishing state
and loot, all four orientations of each scattered structure, live seed-zero
population, generated inventories, and loot. The full native runtime aggregate
passes in 5:14.69 with 441,756 KiB peak RSS and no swap. The checkpoint quick
sweep passes all 14 steps with no skips, and the player-control aggregate passes
all cases when invoked from the repository root. The full sweep passes every
available build, CPU, CUDA, raster, state, and RL gate: 25 `PASS`, zero `FAIL`,
and one explicit `SKIP` because the locally recorded 2026-07-21 canonical tape
and its Mojang-derived frames are absent from this checkout. `BOOTSTRAP.md`
documents that these captures are not distributed. Raster parity itself passes.

The clean three-metric guard passes at 5,014 CPU scalar steps/s, 2.87M Blaze
CUDA environment ticks/s, and 30.77 1080p CUDA frames/s. Their floors remain
3,858.9, 2.793M, and 24.2915 respectively; evidence is
`trace/out/perf_guard_40pct_checkpoint.json`. New work is bounded to active
tiles, carts, hooks, or populated chunks, with no idle full-world scan or
hot-loop allocation added to the base profile.

## 2026-08-07 50% checkpoint

The effort-weighted full-replica estimate is now approximately 50%, with high
uncertainty. It is not a raw feature-count or test-count percentage. The
portfolio is 15 `DONE`, 15 `ACTIVE`, and two `QUEUED` bundles. The increase
from 40% prices three independently gated product slices:

- G-02 moved from `QUEUED` to `ACTIVE`. Village spacing, recursive piece
  selection, all orientation and biome variants, terrain alignment, roads,
  wells, farms/crops, houses, doors, blacksmith chest state and loot now feed
  live population. Generated resident coordinates, profession, and
  zombie-village state are retained as bounded sidecar records. Runtime
  villager entities, rendering, AI, doors/reputation, breeding, golems,
  trading, and persistence remain open and are not implied by this slice.
- R-05 now covers eight dispenser behavior classes in addition to hopper,
  dropper, and rail work: default stone ejection, arrows, splash/lingering
  potions, fire charges, fireworks, oak boats, water/lava buckets, and TNT.
  Java and native agree on spawn position, motion/acceleration, event IDs,
  item mutation, and the stable world-RNG cursor where applicable. Multi-slot
  random selection, item variants, sided/double inventories, and the remaining
  item-specific behaviors remain open.
- L-01 now renders Java's exact 17-point first-person fishing-line centerline
  in both interactive play and frame capture. The focused gate covers the
  camera/hand/bobber transform. Partial-tick bobber endpoint interpolation and
  final line raster/pixel promotion remain open.

Focused Java-to-native gates pass for six village graphs, all 64 relevant
piece orientation/constructor variants and their 60 emitted resident records,
live seed-zero village blocks/loot/resident records, hopper/dropper/dispenser automation,
fishing timers, nested loot, and line geometry. The harsh mesh gate initially
caught a missing village link dependency; the dependency was fixed and the
complete quick sweep then passed 14/14. The full sweep passes 25 available
build, CPU, CUDA, raster, state, and RL gates with zero failures. Its one
explicit skip is the same undistributed local canonical tape documented at
the 40% checkpoint. The native runtime aggregate passes in 5:20.13 with
441,612 KiB peak RSS and zero swap.

The clean three-metric guard passes at 4,869 CPU scalar steps/s, 2.87M Blaze
CUDA environment ticks/s, and 30.48 1080p CUDA frames/s. Their floors remain
3,858.9, 2.793M, and 24.2915 respectively; evidence is
`trace/out/perf_guard_50pct_checkpoint.json`. Village work occurs only while a
population window is built, resident records use fixed arrays, and dispenser
work occurs only for an active scheduled tile. No idle full-world scan,
per-tick heap allocation, or base-profile work was added.

## 2026-08-07 60% checkpoint

The effort-weighted full-replica estimate is now approximately 60%, with high
uncertainty. It remains an estimate of implemented gameplay/render/audio work,
not a raw count of tests or table rows. The portfolio is 15 `DONE`, 16
`ACTIVE`, and one `QUEUED` bundle. The increase from 50% prices three tested
product slices:

- A-01 moved from `QUEUED` to `ACTIVE`. A single ordered, allocation-free
  stream carries represented world, mob, weather, firework, fishing, and trade
  sounds. The interactive client resolves 58 events to 134 owned OGG variants,
  decodes them once, and plays them through a fixed 32-source OpenAL pool.
  Headless and RL paths do not initialize audio. Music, records, ambient loops,
  exact Java variant selection, category settings, and many emitters remain
  open.
- G-02 now materializes ordinary generated residents once, renders exact
  nine-part profession models, and retains lazy merchant state on the
  villager's private RNG. The real-Java comparison covers 11 career selections
  and 22 ordinary initial offers, plus exact prices, matching, two-input
  reversal, use counts, pitch bits, XP/reset rolls, wealth, and first-trade
  willingness. Enchanted/map offers, later tiers/restocking, the merchant GUI,
  AI, reputation, breeding, golems, and persistence remain open.
- R-05 expanded from eight to twelve dispenser behavior classes. Eggs,
  snowballs, experience bottles, and flint-and-steel air/failure/TNT paths now
  join the prior automation set with Java-locked entity/item/event/RNG state.
  Fishing bite pitch also enters the common sound stream using its retained two
  Java float draws.

The focused six-family promotion step passes dispenser, lightning, firework,
fishing, village/resident/trade, and actual OpenAL decode/consume gates. The
clean quick sweep passes 15/15. The full sweep passes 26 available
CPU/CUDA/raster/state/RL steps with zero failures; the one explicit skip is the
same undistributed local canonical tape. The native runtime aggregate passes in
5:10.52 with 441,932 KiB peak RSS and zero swap.

The fresh three-metric guard passes at 5,135 CPU scalar steps/s, 2.87M Blaze
CUDA environment ticks/s, and 31.36 1080p CUDA frames/s. Their floors remain
3,858.9, 2.793M, and 24.2915 respectively; evidence is
`trace/out/perf_guard_60pct_checkpoint.json`. Merchant storage is a fixed
resident-side array initialized only when queried. Audio decoding is an
interactive startup cost (about 40 MiB in the focused test), while event
production uses a fixed ring and headless ticks do no mixer work. No idle
full-world scan or per-tick heap allocation was added.

## 2026-08-07 70% checkpoint

The effort-weighted full-replica estimate is now approximately 70%, with high
uncertainty. This checkpoint deliberately emphasizes trust in represented
behavior. It does not mean 70% of arbitrary saves continue bit-perfectly or
that 70% of rendered frames are pixel-exact.

- The standard Java-oracle path can now export an unopened NoAI villager into
  the versioned state capsule, restore it in magma, and compare the next 20
  ticks. The real-Java run matches 26 compared state categories and all 10,625
  block cells before and after continuation. Profession, age, unopened economy
  state, living-sound timer, and the private 48-bit RNG/Gaussian cache are
  exact. The run keeps the unrelated player `death_time` field explicitly
  unrepresented instead of treating it as a match.
- That comparison exposed a real hidden mismatch: even a NoAI villager consumes
  one private-RNG draw per living tick for its ambient-sound timer. Native now
  advances that timer and cursor in Java order, and the first lazy trade after
  reload remains exact. The focused capsule, village graph, resident, and 22
  ordinary-offer gates pass.
- The hard entity-pixel oracle now captures atomic same-client A/B pairs. All
  16 reference states are stable, so none remain hidden behind capture noise.
  The honest result is zero pixel-perfect C states and 16 residuals. A local
  raster quantization candidate improved the XP crop but doubled unexplained
  whole-scene pixels, so it was rejected.
- The CUDA weather/sky integration now privately remaps all exported symbols.
  The CUDA game links and the five-layer CPU/CUDA raster gate is bit-exact.

The clean quick sweep passes 15/15. The clean full sweep passes all 26
available build, CPU, CUDA, raster, state, and RL gates with zero failures; its
single skip is the undistributed local canonical tape. Full-sweep batched CUDA
throughput is 2.93M environment ticks/s. The fresh three-metric guard passes at
5,094 CPU scalar steps/s, 2.87M Blaze CUDA environment ticks/s, and 31.05
1080p CUDA frames/s against floors of 3,858.9, 2.793M, and 24.2915. Evidence is
`trace/out/perf_guard_70pct_checkpoint.json`. No idle scan or per-tick heap
allocation was added by the capsule work.

## 2026-08-07 strict-equivalence rebase: approximately 68%

The earlier 70% checkpoint is an effort-weighted feature estimate. It is not a
claim that arbitrary Java saves, continuations, or rendered frames are 70%
exact. On the stricter question, "can represented state be loaded in both
engines and compared after every tick?", the current estimate is approximately
68%, with high uncertainty.

State capsule v2 now preserves the full 41-slot player inventory, represented
entities, weather, XP, combat state, effects, equipment, raw blocks, block
light, and skylight. A mixed real-Java continuation matches all 27 compared
state categories for 20 ticks, including all 10,625 cells in each raw-world
plane. A separate XP-orb pickup continuation also matches for 20 ticks. The
permanent gates are `trace/run_mixed_capsule_regression.sh` and
`trace/run_player_xp_pickup_regression.sh`.

The complete native runtime aggregate passes in 6:45.71 with 431,540 KiB peak
RSS and zero swap. A fresh CPU performance guard passes at 4,204 scalar
steps/s against the frozen 3,858.9 floor. This checkpoint did not run GPU
performance work because GPU 1 had a co-tenant.

The remaining strict-equivalence gap is concentrated in arbitrary ItemStack
NBT/container graphs, complete tile entities, the full entity hierarchy and
its hidden AI/path state, scheduled/random-tick queues, unrepresented world
metadata, and global pixel equivalence. Passing bounded capsule gates does not
make unsupported state silently exact: capabilities are versioned and
unrepresented fields remain explicit.
