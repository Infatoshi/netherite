# Minecraft 1.11.2 strict-completeness ledger

Status date: 2026-08-26. Audited in the active completeness worktree.

This is the canonical missing-work list for a complete native, single-player
Minecraft Java 1.11.2 replica. `magma/PARITY_PROJECT.md` remains the history
and focused-promotion log. This file answers the stricter question: what is
still absent, bounded, unproved, or measurably different in an arbitrary valid
save?

The only deliberate cuts are multiplayer/Realms, account/launcher/skin-service
replication, non-vanilla Forge behavior beyond represented hooks, and versions
other than 1.11.2. Everything else in vanilla single-player is open until it
meets the completion rule below. In particular, older survival-only statements
in `magma/PRODUCT.md` are the original product boundary, not a full-replica
completion claim.

The machine count is produced by `verify/completeness/gap_audit.py`. Thirty-
eight rows close directly in this table. Forty-two more have retained strict
receipts in `verify/completeness/strict_closure_receipts.json`: each finite
registry/transition surface passes its fail-closed gate, while any remaining
Cartesian or cross-system breadth is assigned to exactly one of twelve open
canonical owners instead of being counted repeatedly in several rows. The
receipt gate proves that the 38 direct rows, 42 promoted rows, and 12 owners
partition all 92 rows. Thus `80/92` is a closure/work-ownership count, not a
claim that the twelve residual programs or every pixel are complete.

## Direct answers

### Horse

The five horse-family classes now have a live partial implementation. Exact
saved attributes, variants, the six status bits, private animation clocks,
entity RNG/Gaussian state, age/love state, saddle/armor/chest inventories,
feeding, mounting, charged jumps, fall damage, armor mitigation, death/drop,
audio routing, exact horse/donkey genetics, mule birth, the 60-tick mating
task, untamed run/tame/eject AI, exact owner UUID persistence, and
checkpoint/cold-Anvil continuation are represented. Skeleton-trap activation
now constructs the exact seven-entity rider group and effect-only lightning,
including UUID/equipment/RNG streams, loaded update order, transient baby-to-
adult bounding boxes, first-tick bow navigation, collision pushes, mount pose,
and exact position, motion, and entity-RNG rows through tick 120. The promoted
combat continuation includes the bow AI's approach-to-strafe transition,
accepted horse wandering with the 1.11.2 A* pathfinder, and all 26 observed
arrow rows across four projectiles, including shooter, age, motion, and RNG. A
strict real-Java fork covering all five classes is exact at every tick through
20, including nonzero owner UUIDs, raw blocks, sky light, block light, and
height. The automatic birth boundary is also full-world exact for all four
horse/donkey parent orderings: Java and native match child type and genetics,
parent and child state, loaded/update order, first physics update, entity and
world RNG, particles, XP, and IDs. Nine full-world taming boundaries are exact
across horse, donkey, and mule success, failure, and no-trigger paths,
including AI task clocks, RNG, owner assignment, movement, mounted pose,
side-dismount placement, and ordered sound/status events. Explicit sneak
dismount is additionally bit-exact to Java for open, one-blocked, and
twice-blocked layouts across nine cardinal, diagonal, and near-cardinal yaws
for both primary hands. The 54 cases include the raised target, over-horse
fallback, AABB, motion, and passenger graph. The live runtime carries the
primary-hand choice. Mount replacement, mounted pose, and recursive
horse/player fall damage have narrow regressions.
Breeding particle payloads now reach the renderer without loss. Horse status
6/7 expands to seven normal-smoke/heart calls with client-side positions and
the final Java Random cursor bit-exact to a real 1.11.2 oracle, while keeping
the server entity RNG untouched; both particle factories, tick lifecycles,
and layer-0 billboard paths have native regressions. An atomic real-client
frame fixture also captures each particle's private constructor state. Against
a same-scene horse control, smoke has 50 and heart has 9,270 nonempty subject
pixels; Java A/B noise is zero and native ownership and RGB are exact for both.
The horse inventory screen is also live rather than a render mock: the checked
container path enforces adult/tame access, saddle and horse-only armor slots,
all fifteen donkey/mule storage slots, Java shift-click order, invalid-stack
rejection, distance close, and persistent slot ownership. Its ordinary-horse
panel has no pixel above max-channel 25; the chested-donkey preview retains 22
measured fixed-function edge pixels above that threshold.

The skeleton-trap render fixture now stages all four real horse/rider pairs
and freezes all eight client entities atomically in each Java A/B render. Java
A/B is zero-noise. Matching `RenderLivingBase`'s disabled face culling on the
skeleton base and helmet layers removed the large transparent-helmet and body
holes: the isolated rider fell from 538 to 77 pixels above max-channel 25 and
the full group from 3,574 to 2,989. Of the remaining group hard pixels, 1,667
are in common-owned interiors. These are regression-locked residuals, not an
exact-pixel completion claim.

`ENT-02` remains open for those 22 horse-inventory fixed-function edge pixels
and the bounded world-model and skeleton-trap edge tails.

Llama is now live-partial. Taming/feeding, reciprocal caravan links, leads,
chests/decor, strength-bounded storage, the container/UI, breeding/genetics,
death/loot, audio, client particles, spit motion and cross-store target
selection/damage dispatch, models/layers, persistence, and a four-tick
real-Java/native continuation are implemented. The collision matrix covers all
currently represented entity stores; eleven target classes have a direct real
Java oracle. Five direct real-Java caravan rows lock in-range grace aging,
both acceleration boundaries, expiry, and reset retention. Four direct
priority-conflict rows lock caravan suppression of ranged and mating while
swimming and follow-parent remain concurrent. A 16-tick child fixture matches
the materialized follow-parent path, position, velocity, and entity RNG cursor
through an airborne boundary and the ten-tick navigator refresh. Accepted
wander, watch-player, and idle-look starts also match Java in task mask, RNG
cursor, and all six position/motion doubles. Two 12-tick panic fixtures cover
nearest-water selection and the random-position fallback, and a 16-tick mating
fixture crosses an airborne entity-target request. Their complete paths, task
masks, RNG cursors, contact states, and position/motion doubles are bit-exact.
Java A/B for the llama inventory is zero-noise; native now has no pixel above
max-channel 25
after matching the carpet model's one-row side-wall UVs, with four bounded
pixels above channel delta 1 and maximum 10. A second exact terrain run now
locks `PathWorldListener`: a distant collision edit retains the active path,
a nearby wall opening replaces it, and all eight post-edit motion rows remain
bit-exact. It also corrected the shared horse-family step height from 0.6 to
Java's 1.0 blocks. `ENT-03` remains open for ranged navigator edge conflicts,
foreign-target terminal details owned by the
remaining entity rows, and closure of bounded fixed-function world/UI tails.

### Ender Chest

Ender Chest is live-bounded. Activation blocking, the player-owned 27-slot
inventory, cross-dimension ownership, container routing, exact lid clocks,
open/close audio, display particles, collision, Silk Touch harvest, checkpoint
continuation, and real-Anvil `EnderItems` import are represented. A populated
real-Java inventory panel is pixel-exact across all 116,720 owned pixels. The
in-world tile uses `entity/chest/ender.png` and the interpolated live lid in
both interactive and frame-capture renderers; closed/open Java A/B is
zero-noise and native has zero subject pixel above max-channel 25, with a
regression-locked maximum of 19. `UI-04` remains open for the other screens and
`VIS-08` for the other tile renderers.

### Wooden Chests

Ordinary and trapped chest tiles now use the real single/double entity
textures, exact ModelChest/ModelLargeChest boxes, all four metadata rotations,
live interpolated lids, and Java's orientation-dependent double-chest
translation in both interactive and frame-capture renderers. Six closed/open,
normal/trapped, and X/Z double fixtures have byte-exact Java A/B controls.
Four single states have no owned pixel over four channels; the X double has
one bounded pixel (maximum 14), and the Z double has two exact-coordinate
minified edge/texel ties. The latter are regression-locked rather than hidden
by a broad allowance. Gameplay, inventory, audio, comparator state, and save
continuation were already live-bounded under the chest runtime.

### Armor Stand

Armor Stand is now live-bounded rather than render-only. Placement, ItemStack
`EntityTag`, all six equipment slots and disabled masks, six-part pose, status
flags, player interaction, two-hit break, arrows, explosions, fire/lava/void,
water/rain/fall behavior, rideable-minecart contact, drops, sounds, particles,
rendering, checkpointing, and cold Java-save continuation are represented.
The saved-state boundary now also carries custom names/visibility, tags,
absorption and base/effective maximum health, potion effects, revenge/portal
timers, silent/glowing/invulnerable/update-blocked/fall-flying flags, and a
minecart vehicle reference. Regeneration timing and Health Boost attribute
expiry/clamping follow Java. Built-in skull types 0 through 5, Elytra,
arbitrary held blocks, and arbitrary block/item head stacks have native render
paths.
Placement now rejects intersections across every represented live entity
store, and one global nearest-candidate pass orders attacks across Item Frames,
Armor Stands, minecarts, crystals, Withers, shulkers, the dragon, and ordinary
mobs instead of allowing store order to select a farther target.
Fourteen direct real-Java interaction/damage rows lock equipment swaps,
disabled/name-tag outcomes, both punch edges, arrow/explosion/creative breaks,
fire, void, ordered drops, exact sounds/status, and particle descriptors. A
rich real-Java save is Java A/B exact and native exact at every tick through
20 with zero rejected fields and exact raw blocks/light/height. The oracle also
locks vanilla's deliberate NBT reset of age, punch cooldown, last-damage, and
entity RNG transients.

`ENT-04` remains open for profile-provided custom skull textures and strict
same-scene Armor Stand pixels, including the visible-name plate and its
outline. The
external skin-service itself remains one of the explicit product cuts above.

### Glowing

Glowing is live for represented entities. Active effect 24 and saved glowing
flags reach the render view, and invisible glowing living entities are restored
only in the outline pass. Capture and interactive composition implement the
1.11.2 entity-outline framebuffer, two Sobel passes, and final source-alpha
blit. RenderLivingBase's cull-disabled model state is retained, including the
Slime gel layer; Magma Cube correctly has no gel layer.

The deterministic Magma control/Glowing pair is byte-stable in Java A/B and
channel-exact in native at every final pixel changed by either implementation.
A second Slime fixture locks the translucent stress path with an exact outline
bounding box and a mutation-tested ceiling over its pre-existing,
pxdiff-classified fixed-function shading-offset tail. The hard gate also
rejects missing render pins, wrong requested Glowing state, and zero-support
captures. Glowing itself is closed under `ITEM-05`; strict class-specific model
residuals remain owned by `ENT-*` and `VIS-*`.

### Wither

The boss Wither is now live. Summoning, invulnerability birth, three-head
target/shot scheduling, normal and blue skulls, block breaking, armor phase,
healing, ordinary player damage, death, Nether Star and XP construction,
audio, particles, boss bar, rendering, hidden state, checkpointing, and cold
Java-save continuation are implemented and directly tested. The boss bar and
label are strict pixel-exact against stable real-client goldens.

`ENT-01` is closed. A same-scene background control isolates the rendered
subject without masking model errors. Normal and invulnerable Withers have zero
pixels above max-channel 25, and the armored-minus-normal aura differential is
also zero; each Java A/B control is zero. The native maximum channel deltas are
1, 5, and 8 respectively. This locks the exact `-1.501` model translation,
Minecraft's `MathHelper` animation/scroll LUT, cull-disabled two-sided aura,
additive depth-write state, D24 conversion with a measured four-unit depth
bias, and the client's 8-bit subpixel endpoint grid. Wither Skeleton and the
Wither potion effect remain separately implemented and tested.

### Redstone

Redstone works. The current full-runtime matrix contains 735 literal named
cases, of which 537 are redstone cases. Live paths cover dust, torches and
burnout, levers, buttons, all pressure plates, tripwire, lamps, repeaters,
comparators and many override sources, six-face observers, normal/sticky
pistons in all directions, slime and 12-block movement, hoppers,
droppers/dispensers, detector/powered/activator rails, and several minecart
types. The piston promotion alone records 576 behavior/raw cases in the
project ledger.

"Redstone incomplete" means the finite promoted cases are not yet a proof for
every arbitrary circuit. The strict tail is random topology and neighbor-order
coverage, cross-chunk unload/reload, all pending-tick save boundaries, the
remaining piston/slime/collision shapes, every automation payload and cart,
and removal of proof-capacity assumptions. Current notable capacities are 256
dust cells per component traversal and 64 moving-piston records. Scheduled
ticks now grow from a 4,096-entry hot-path allocation to a fail-closed
1,048,576-entry safety ceiling; the 4,097th insertion, checkpoint, and restore
are regression-tested. See `RED-01` through `RED-08`.

## What strict-complete means

A subsystem is complete only when all of these are true:

1. Every applicable Java registry entry is classified and supported. A known
   rejection is an open gap, not a pass.
2. A real Java save can be loaded at the same causal boundary on both sides.
   No required NBT field, loaded order, scheduled tick, tile, entity, RNG
   cursor, or transient reconstruction rule is silently dropped.
3. With the same tick clock and input sequence, the first and every subsequent
   state match: raw block state and light, queues/order, entity and player
   scalar bits, complete semantic NBT, inventories, events, and relevant RNG
   state.
4. Save/reload at an intermediate tick gives the same continuation as Java.
   The uninterrupted and reloaded branches are both tested.
5. Stable owned pixels match the Java-vs-Java noise floor. Recorder-private
   entropy must be captured or explicitly made deterministic in both oracle
   and native; it cannot be hidden by a residual budget.
6. A negative control proves each comparator detects the intended old/wrong
   behavior.
7. CPU and CUDA agree for shared simulation surfaces, all aggregate gates pass,
   and performance/RAM remain within the frozen guardrails.

`live_bounded` therefore means useful and directly tested, not universally
complete.

## Current strict boundary

- The authoritative Java registry contains 81 entity registrations. The
  machine-checked ledger is
  `verify/completeness/registry_manifest.json`; all 81 now have a live-bounded
  native boundary, with no live-partial, state-only, render-only, or absent
  entity row. This is exhaustive registry identity coverage, not strict
  lifecycle closure: the owning `ENT-*`, `AI-*`, `DIM-*`, `ITEM-*`, and
  `WORLD-*` rows below retain their arbitrary-state and pixel tails.
- The Java tile-entity registry contains 24 registrations. All 24 now have a
  live-bounded native boundary, with no live-partial, state-only, render-only,
  or absent tile row. Structure Block has exact modes, transforms, stable
  placement order, integrity RNG, redstone edges, and represented entity/tile
  payloads. The integrated-server command census is machine-checked at 47
  vanilla classes; five are direct Java/native exact: `time`, `weather`,
  `gamerule`, `toggledownfall`, and `seed`. Command blocks also retain the
  bounded `Searge` easter egg. The owning rows below still
  track UI, arbitrary scheduling/NBT composition, and strict pixel gaps.
- Exact fresh-object Java-NBT continuation currently covers 34 plain native
  living classes for 20 ticks, plus XP/item loaded-order sentinels. The common
  living schema includes bounded active effects with amplifier, duration,
  ambient and particle flags, effect-derived maximum health, and absorption.
  XP orbs retain their authoritative AABBs rather than reconstructing them
  from a rounded center. Active Villager has its own bounded 20-tick
  continuation. Arbitrary AI task lists, path state, equipment/tag NBT, and
  cross-store order are not covered.
- State capsule v2 carries exact loaded chunk topology, scheduled ticks,
  hidden entity state, arbitrary ItemStack NBT, and represented cross-store
  update order. Ordinary/trapped single/double chest and Shulker Box animation
  transients now continue from raw Java state. Arbitrary mixed-registry update
  ordering remains `AI-05`.
- Native player-facing disk save/load is implemented through atomic immutable
  generations and the pause/title world-selection UI. Survival is the only
  exposed game mode. Creative, adventure, spectator, hardcore, commands, and
  several vanilla screens remain open under the promoted full-replica scope.
- Core HUD, inside-block overlays, and GUI action/chrome fixtures are exact.
  Ordinary/trapped double chests now have a live canonical 54-slot container,
  exact populated real-Java panel pixels, and large-GuiChest tape replay.
  Inventory preview, hand-use poses, portal, underwater, all 16 focused
  entity/particle ROIs, and several full-tape clusters remain strict nonzero
  pixel failures.
- The clean native suite and quick sweep pass. The latest focused performance
  rerun reaches 4,658 scalar CPU steps/s and 2.85M CUDA env-ticks/s on GPU 1,
  above the frozen 3,858.9 and 2.793M floors.

## The save-fork differential harness

The completion wave starts by generalizing the existing fail-closed capsule,
not by replacing it with a permissive loader.

For each case, create a real Java world and save it immediately before the
causal boundary. Call that save `S0`. Fork it as follows:

```text
S0 -> Java reload  -> inputs I[0..N) -> J0, J1, ... JN
  \-> native load -> inputs I[0..N) -> M0, M1, ... MN
```

At a selected tick `K`, save and reload both branches, then continue. Compare
the uninterrupted and reloaded branches as well as Java against native. Use
three default horizons: 1 tick for first cause, 20 ticks for local lifecycle,
and 200 ticks for delayed work. Longer mechanics use their natural terminal
tick.

Every fixture also has a neighboring save or input that differs in one causal
fact. Examples are a pending callback due at `t` versus `t+1`, Wither
invulnerability 1 versus 0, horse temper 99 versus 100, a powered rail with
one neighbor changed, or a random-tick section included versus excluded. This
prevents a comparator from passing on an inert scene.

The per-tick comparison vector is:

- world/dimension clocks, weather, difficulty, gamerules, world border, and
  every represented RNG cursor;
- loaded chunk/section membership and order, raw block ID/meta, sky/block
  light, height data, pending block ticks with priority/tie order, and tile NBT;
- entity loaded/update order, type, ID/UUID, passengers/leashes/owners, complete
  semantic NBT, raw float/double bits, AABB, timers, attributes, effects,
  equipment, AI/task/path state, and private RNG/caches where they influence
  continuation;
- player state, complete ItemStacks, every open container slot/cursor,
  statistics/achievements, and input acknowledgements;
- ordered block/entity/status/sound/particle events; and
- pixels at stable causal keyframes, with Java A/B stability measured first.

The harness must stop at the earliest divergent field, retain `S0`, inputs,
both traces, and keyframes, and minimize the fixture without changing that
first divergence.

## Ordered completion queue

The tables are ordered. Within one feature, the saved-state identity case is
implemented before the transition case, and scalar/world parity before pixels.

### 0. Harness and census

| ID | Gap and required run | Strict exit |
|---|---|---|
| `HAR-01` | Registry completeness gate for all Java entities and tile entities. | **DONE:** `registry_gate.py` fails on an unclassified registry row or missing TODO. |
| `HAR-02` | Export a complete authoritative post-reload Java snapshot, not only fields needed by one fixture. Include registry identity and an explicit present/absent marker for every comparison field. | **DONE:** the locked exporter carries full entity/tile NBT, RNGs, identity/order, tick queues/tie IDs, navigation paths and executing AI order; generated field inventory classifies every observation, Java cold A/B is exact, and deleting an exporter field fails the snapshot hash gate. |
| `HAR-03` | Replace implicit capsule fences with a generated accept/reject capability report for every player, entity, tile, chunk, and world field. | **DONE:** schema-v2 snapshots inventory every observed Anvil/hidden field and fail closed with a named TODO; `save_fork.py selftest` covers omission/hash rejection. |
| `HAR-04` | Implement the `S0` fork runner with exact tick/input clocks and 1/20/200-tick horizons. | **DONE:** one command cold-runs Java A/B and native, requires represented state plus block/light/height identity at t=0, advances the same input sequence through arbitrary selected horizons, compares every state tick and raw horizon, and emits the earliest unsupported or divergent field/tick. A fresh-save negative run reports statistics at t=0 and, beneath that capability fence, random-tick RNG at t=1. |
| `HAR-05` | Add full semantic NBT, raw numeric, block/light/queue/order, event, and pixel comparators. | **DONE:** `comparator_gate.py selftest` mutates and reverses all eight families and rejects zero-frame pixel comparisons. |
| `HAR-06` | Add automatic first-divergence reduction over cuboid size, entity/tile set, input prefix, and horizon. | **DONE:** `reduce_failure.py selftest` reduces a seeded multi-cause case to the known earliest cell/entity/input/horizon. |
| `HAR-07` | Make paired-boundary and negative-control definitions mandatory in fixture metadata. | **DONE:** strict `fork_runner.py` requires a validated fixture; `fixture_contract.py selftest` rejects missing alternate branches, inert mutations, short inputs, and zero observations. |
| `HAR-08` | Capture recorder-private facts that currently make comparison impossible: particle spawn calls/constructor inputs, render partial ticks, relevant client/server clocks, and complete GUI interactions. | **DONE:** the isolated real-client gate records exact constructor state for explosion normal/large/huge, normal smoke, spell, and heart particles, render partial and client/server clocks on every tape row, raw GUI input and semantic window actions with complete result-stack NBT. Explosion, cloud, weather, hand, inventory, and horse-status cases are nonempty; each pixel fixture has zero Java A/B drift. |
| `HAR-09` | Generate registry coverage reports and candidate fixtures from Java registries/overrides rather than hand-maintained prose. | **DONE:** the live Java exporter plus checked gate enumerate all 81 entities, 24 tiles, 236 blocks and callback/comparator overrides, 400 crafting recipes, 51 smelting recipes, 27 effects, 37 potion types, 81 loot tables, and 13 container/13 GUI classes. Every row has a unique generated fixture candidate and an existing owning TODO; source counts and optional fresh-Java equality fail the gate when the census changes. |

### 1. General save/load and ordering

| ID | Gap and discriminating save-state case | Strict exit |
|---|---|---|
| `SAVE-01` | Load a real Anvil-format 1.11.2 save into the neutral importer, including `level.dat`, player, dimensions, chunks, and region data. Pair pristine save with one changed block/tile/entity. | **DONE:** the lossless semantic reader plus native importer retain every persisted column in all dimensions, exact block/meta/sky/block light/biome/height payloads, player and level data; a 1,305-chunk evolved save pages exactly at t=0, and unsupported entity/tile fields fail against their named TODO instead of falling back. |
| `SAVE-02` | Add native player-facing save, quit, world selection, and reload. Compare Java save->Java/native and native save->native semantic state. | **DONE:** atomic immutable-generation slots retain every represented checkpoint, world-column, and statistics field; fresh-process load/resave continuation is exact, corrupt/unpublished generations fail closed, the SDL pause/save/world-list/reload path is exercised under Xvfb, and a fresh Java A/B/native fork is exact at 1/20/200 ticks with zero rejected fields. |
| `SAVE-03` | Preserve or exactly reconstruct loaded chunk, tile, entity, passenger, and update order. Use identical objects inserted in opposite orders. | **DONE:** checked real-Java forks retain exact 289-chunk provider order, loaded/tickable tile lists, loaded entity order, and a mounted player/minecart graph. Forward/reverse item pairs genuinely unload and reload by UUID, then select opposite merge survivors; forward/reverse hoppers write opposite chest slot order. Every branch is Java A/B exact and native exact at t=0/1, the passenger graph remains exact through t=20, raw block/light/height horizons match, and all imports have zero rejected fields. |
| `SAVE-04` | Carry arbitrary ItemStack and EntityItem NBT: names, lore, enchantments, attributes, potion/firework data, books, maps, capabilities that are in scoped vanilla state, and nested container tags. | **DONE:** content-interned canonical NBT is retained through player, EntityItem, chest/furnace/static-container, and minecart stacks, native checkpoint/reload and full Java save forks. All 392 Java registry rows preserve arbitrary nested tags through reconstruction/split; equal/unequal tagged merge and pickup cases match native. The real-shaped corpus includes every listed family, nested arrays/containers, and adversarial tag names; malformed payloads and dangling handles fail closed. |
| `SAVE-05` | Generalize entity persistence beyond bounded NoAI schemas: UUIDs, attributes, equipment, effects, passengers, owners/leashes, hidden counters, per-class fields, and reload reconstruction. | **DONE:** every persisted and active entity is joined by UUID to all 81 generated registry rows, then either reconstructed by a complete class schema or rejected against that row's named implementation TODO. Unknown, inactive/cold, duplicate, mismatched, and order-ambiguous entities fail closed; opposite loaded-order EntityItem forks remain exact. |
| `SAVE-06` | Generalize all 24 tile-entity NBT schemas, deferred loot, custom names/locks, progress, open state, and tick reconstruction. Pair each tile at adjacent progress/counter values. | **DONE:** all persisted and loaded tiles are classified against all 24 generated registry rows and their full NBT identity. Complete native schemas round-trip; partial/absent schemas, nonempty unsupported locks/names, inactive tiles, and order ambiguity fail against their owning TODO. Opposite tickable/loaded hopper orders remain exact through their first mutation. Ordinary/trapped single/double chest viewer count, raw lid floats, and private sync counter restore and continue exactly against real Java; Shulker Box viewer count, animation status, and both raw progress floats do the same through the open clamp. |
| `SAVE-07` | Restore complete scheduled-tick queues, priorities/ties, random-tick active sections, chunk membership/order, and all world/process RNG facts needed after Java reload. | **DONE:** due-at-`t`/`t+1` falling callbacks, both equal-time stacked insertion orders, and selected/missed natural random ticks preserve exact queue rank, loaded/ticking chunk order and masks, raw height/light, world/math/block/server-UUID cursors, `updateLCG`, and saved `randomTickSpeed`. Java A/B and native are exact at 0/1/20/200 ticks; the negative pair mutates cactus age only on the selected branch. |
| `SAVE-08` | Mid-action reload corpus: eating/use, bow draw, fishing hook, potion/cloud, piston motion, repeater/observer pulse, TNT/falling block, furnace/brewing, villager task, mob attack, vehicle, dragon ritual, and weather transition. | **DONE:** strict real-save fixtures cover active eating and bow cancellation, dropped fishing hooks, persisted potion/cloud entities, moving pistons and ordered tiles, pending repeater/observer pulses, primed TNT plus falling sand, furnace/brewing progress, executing villager and golem-attack state, moving mounted minecarts, End-ritual cancellation, and reconstructed weather interpolation. Every final fixture is Java A/B exact and native exact with zero rejected fields and exact raw block/light/height horizons. |
| `SAVE-09` | Chunk unload/reload and dimension leave/return with entities, tiles, structures, portals, villages, and pending work near borders. | **DONE:** twenty real-Java watcher/provider unload/reload cycles and twenty parked dimension round trips preserve the fixture entity UUID/count, loaded entity and tile order, chest/furnace payloads, cross-border portal, village door/clock, and near-border callback without hidden transfer ticks, duplication, loss, reorder, or callback revival. A cold Java A/B/native fork is exact at 0/1/20 ticks with zero rejected fields and exact raw block/light/height; all 2,227 serialized Nether callbacks (1,815 unique after vanilla border-overlap deduplication) remain dimension-scoped, and native checkpoint regression repeats twenty leave/return cycles. |

### 2. Missing and partial entity registry families

| ID | Gap and discriminating save-state case | Strict exit |
|---|---|---|
| `ENT-01` | **Wither boss and Wither skull. DONE:** exact cold/uninterrupted forks cover summon, invulnerability 1/0, targeting/head cooldowns, skulls, armor/heal, block break, lethal/mid-death, Nether Star/XP, ordered RNG/UUID state, audio and particles. The real-client boss bar is bit-exact. Same-scene subject pixels close normal, invulnerable, and armored-aura states to zero pixels above max-channel 25 against zero-noise Java A/B controls. | **DONE:** boss and projectile lifecycle, persistence, events, audio, particles, UI, model, aura, and focused pixels pass. |
| `ENT-02` | **Horse family. LIVE BOUNDED:** all five classes have exact saved attributes/status/RNG/animation/inventory/owner state and a strict Java A/B/native 20-tick cold continuation, including nonzero owner UUIDs. Feeding, saddle/armor/chest storage, mounting, charged jump, fall, damage/death/drop, audio, exact four-way horse/donkey genetics, mule birth, the live 60-tick mating task, and measured run/tame/eject AI exist. All four horse/donkey automatic birth boundaries and nine horse/donkey/mule taming boundaries match a real full `World.updateEntities` tick exactly, including first updates, order, RNG, physics, passenger placement, owner state, and events. Status 6/7 client particles and the automatic-birth heart render pipeline are locked. The live horse container covers all fifteen chest slots and persistence. The skeleton-trap group's construction, navigation, combat, RNG, bow-strafe transitions, and all 26 projectile rows match Java through tick 120. Horse-specific passenger edges include 54 exact Java/native dismount cases. Still open: measured fixed-function inventory/world-model/skeleton-trap pixel tails. | All five registry classes are playable, persist exactly, and pass state/event/pixel cases including horse GUI. |
| `ENT-03` | **Llama and llama spit. DONE:** taming/feeding, reciprocal caravan links, leads, chests/decor, strength-bounded storage, the container/UI, breeding/genetics, death/loot, audio, exact client spit particles, spit motion, represented-store target selection/damage dispatch, models/layers, persistence, and a four-tick real-Java/native continuation exist. Eleven foreign target classes have a direct real-Java oracle; the native matrix locks nineteen target kinds. Direct real-Java fixtures cover caravan clocks, task conflicts, lower-priority tasks, panic, mating, follow-parent, ranged combat, obstacle navigation, and dynamic replanning. The llama inventory has zero-noise Java A/B and no native pixel above max-channel 25. Still open: foreign-target terminal details owned by `ENT-05`/`ENT-06`/`AI-04` and bounded fixed-function world/UI pixel tails. | **DONE:** Live llama/spit, persistence, inventory/UI, AI, audio, model layers, and pixels match. |
| `ENT-04` | **Armor stand. LIVE BOUNDED:** placement plus `EntityTag`, six equipment/pose slots, status/disabled flags, interaction, two-hit break, arrow/explosion/fire/lava/void/environment paths, rideable-minecart passenger continuation, drops/events, checkpointing, and a rich real-Java save fork are implemented. Generic names/tags/effects/attributes/timers/entity flags are preserved; Regen and Health Boost expiry are live. Built-in skulls 0-5, Elytra, held blocks, and arbitrary block/item head stacks render. Placement checks every represented entity store and attacks use one cross-store nearest selector. Fourteen direct Java behavior rows lock interaction/damage/drop/audio/particle outcomes. Java A/B and native state/raw horizons are exact through tick 20 with zero rejected fields. Still open: profile custom-head texture and strict same-scene nameplate/glow/layer pixels. | Placement, interaction, pose NBT, equipment, collision, damage/drop, reload, render layers, and pixels match. |
| `ENT-05` | **Hanging/leash family. LIVE BOUNDED:** all three cold-growable stores cover exact pose/AABB and 100-tick support clocks, surface/overlap checks, player placement/attack/interact routes, drops/sounds, UUID membership, checkpointing, authoritative export, and capsule restore. Item frames preserve arbitrary tagged/enchantable stacks, rotation, drop chance/RNG, comparator notifications, two-stage damage, projectiles, explosions, creative edges, and the exact 16,384-byte filled-map plane plus marker/tracker state. Two hundred twenty-seven direct real-Java rows lock geometry, lifecycle boundaries, painting constructor choice across all 26 arts plus six restricted support shapes, every permutation of mixed frame/painting/knot update order, the complete hanging damage equivalence matrix, 32 map-marker cases, and all 16 vanilla leashable living classes across player attach, fence transfer, pull, break, squid, sitting-pet, horse-eating, and angry-wolf edges. Painting art with per-tile light, both item-frame baked models, block/generated items, dynamic maps/icons, the knot model, and both living-leash strips now render. The same-scene Java A/B gate has zero oracle noise; native has 0/3/0/1/17/0/0/2 pixels above max-channel 25 across Kebab/Pointer/empty/stick/dirt/map/knot/leashed-llama. The prior 304-pixel leash gradient was a real bug and is fixed by reproducing Java's flat provoking-vertex color. Still open: the measured 23-pixel fixed-function coverage tail. | All three lifecycles, collision/raycast, NBT, drops, comparator notifications, maps, rendering, and pixels match. |
| `ENT-06` | **Projectile tail. DONE:** all ten registry rows owned here are machine-checked `live_bounded`. Ender Pearl covers launch, flight, player/mob/block impacts, teleport/damage/endermite RNG, End Gateway behavior, Java NBT reload, neutral capsule restore, foreign-dimension loaded-area admission, and Nether-portal reconstruction/continuation. Existing-portal destination placement is exact across 52 Java/native rows covering both portal axes, every horizontal entry direction, off-center entries, and obstruction fallbacks; portal search, source-shaped construction, scaled-coordinate clamping, cache/pruning, checkpoint restoration, and reverse continuation are locked. Spectral and tipped arrows cover launch/ammo/Infinity, configurable effects, pickup, block/entity impacts, complete motion, UUID/RNG, semantic NBT, and checkpoint continuation. Egg, snowball, XP bottle, Ender Eye, large/small/dragon fireballs, potions, rockets, Wither Skull, Shulker Bullet, and Llama Spit have separately owned bounded paths, so every projectile registry identity has launch, motion, collision/terminal, payload, persistence, and event evidence. Strict arbitrary mixed-store collision permutations, foreign-dimension event delivery, arbitrary firework particles, and isolated pixels remain in `AI-05`, `ITEM-05`, `ITEM-06`, and `VIS-*`. | **DONE:** every projectile registry row is fail-closed and maps to direct lifecycle, terminal-effect, reload, event, and render ownership evidence. |
| `ENT-07` | **Minecart variants. DONE:** all seven registry carts are independently bounded. Their movement, collisions, riders, activator/detector/powered rails, destruction/drops, inventory/fuel/push/smoke state, interactions, represented NBT, Structure persistence, and concrete render payloads pass direct Java cases. Spawner carts carry exact typed current `SpawnData` plus all weighted potentials through Java export, capsule sidecars, checkpoint and Structure state; conditional `Pos` RNG, constructor/UUID/Math RNG, class-aware caps, collision/admission, default initial spawn, represented custom living NBT, weighted reset, and persisted loaded-entity interleaving share the block-spawner implementation. A fresh custom saddled NoAI-pig cart is Java A/B/native exact at horizons 1/2/3 across state, order, NBT, blocks, light and RNG. Command carts retain command/name/output/success/track state, reset Structure-only transient clocks, checkpoint, and match Java's four-tick powered-activator cooldown plus built-in `Searge` execution through tick 9. Structure templates preserve all seven classes, exact base/display state and represented subtype payloads, fresh UUID/constructor RNG, and transient reset. Live and tape render views retain every subtype. Twelve zero-noise real-Java states lock all six non-TNT models plus unprimed/fuse 80/79/4/5 TNT. Native reproduces the exact cart shell, ModelChest transform/texture, hopper baked elements and coordinate UVs, directional furnace/command faces, spawner cage, entity-id registration, quartic TNT swell, and opaque flash phases. Empty, furnace, and command are at the channel-one noise floor; remaining unscaled variants retain only classified fixed-function shading samples. General command dispatch remains owned by `MODE-02`, arbitrary unsupported spawner entity tags by `WORLD-06`, and the command editor by `UI-02`. Also open are deferred worldgen chest-cart loot lifecycle, arbitrary non-cube custom displays, and broader strict rail topology/save cases. | **DONE:** All seven registry carts pass adjacent rail/junction/save cases and close pixel lineup. |
| `ENT-08` | **DONE:** all six wood identities, rendering, placement, mounting, attacks, explosion contact, Structure/capsule identity, and passenger pose are represented. A direct Java/native oracle bit-locks all five Boat status states, air-to-water entry, rider input, exact 60-tick submerged ejection, passengers/pickup/pushes, fall destruction/drop suppression, correction, and all private motion/paddle fields. Production movement uses the shared exact block-AABB resolver; 5,870 exact rows cover every valid metadata state of fifty shaped collision families, every four-direction approach and landing, all sixteen connected-neighbor masks, Slime glide, and non-living bounce. Portal replacement is exact across both axes, all entry directions and off-center vectors, including identity, constructor RNG, cooldown, dimension ownership, and checkpoint continuation. Save/load retains Boat state and passenger graphs, and the 503-tick riding tape remains gated. A stable real-client fixture covers all six Boat textures against one same-scene background: Java A/B noise is zero, Java/native ownership is identical apart from five threshold-edge pixels on Birch, and only seven measured minification tie samples exceed channel delta four. The maximum tail is one Acacia texel-selection sample; the mutation-tested bounded gate rejects geometry, registration, variant-dispatch, or texture regressions. | **DONE:** state, collision, rider/passenger behavior, portal transfer, save/reload, all six wood textures, and bounded isolated pixels pass direct Java/native gates. |
| `ENT-09` | **DONE:** dynamic world-facing stores grow without silently losing represented state. Living entities retain the 95-slot hot page for the common fast path and spill into a generated cold record containing all 455 per-entity fields; arbitrary growth preserves loaded/tick order and stable EID identity across targeting, mating, caravans, leashes, mounts, collisions, effects, village queries, rendering, and checkpoint continuation. A 110-entity regression crosses the former limit and checks spawn, tick, render views, collisions, cloud cooldowns, two-cold interactions, mating, llama caravans, pig/horse/boat mounts, and reload. Renderer entity/leash staging and village enumeration size from the dynamic living census. `living_capacity_census.py --strict` rejects any uncovered field, bypass spawn, unreviewed hot-cap scan, or raw loaded-reference slot access; it reports 455/455 fields covered and zero residual sites. XP, item, tile, structure, piston, scheduled-tick, projectile, boss, weather, audio, particle, loot/drop, and causal-order stores have below/at/above saturation and checkpoint tests; multi-output operations grow or reject atomically without consuming RNG, IDs, inventory, state, or events. | **DONE:** arbitrary represented capacity is growable or explicitly safety-bounded and fail-closed; order, identity, rendering, checkpoint continuation, and the fixed hot-path performance floor pass. |

The `ENT-09` capacity battery also covers variable-length custom item names at
the exact 65,535-byte NBT boundary and dynamically grown block-spawner,
spawner-minecart, and Structure-template `SpawnPotentials` lists past the old
16-row ceiling, including deep checkpoint and Structure placement continuation.

### 3. Existing living entities, AI, and global ordering

| ID | Gap and discriminating save-state case | Strict exit |
|---|---|---|
| `AI-01` | **DONE:** all seventeen hostile registry classes owned here are machine-checked live boundaries: zombie/skeleton variants, Creeper, spiders, Enderman, Blaze, Ghast, Pigman, Slime/Magma, Silverfish, Endermite, and Giant. The shared matrix covers priority/target/path/revenge, melee/ranged/special attacks, equipment/effects/environment, despawn, terminal drops/XP, persistence, audio/events, and render handoff; dedicated Java/native gates cover Husk, Stray, Slime, Endermite, and Giant boundaries. Endermite includes pearl construction/RNG and lifetime continuation; Giant locks its deliberately empty task lists, scale-six state, empty loot, and five-XP death. Natural pack selection has a represented weighted hostile selector. Strict pack enumeration in arbitrary biomes, every active-task terrain conflict, type-specific step/portal particles, and isolated model pixels remain owned by `AI-05`, `WORLD-*`, `AUD-02`, and `VIS-05`. | **DONE:** every owned hostile registry class has playable task/combat/special/death/save/event/render evidence and no generic identity fallback. |
| `AI-02` | **LIVE BOUNDED:** Villager, Iron Golem, and Zombie Villager are all machine-checked live registry boundaries. Dedicated native and real-Java gates cover door discovery/state, center/radius, residents, reputations/aggressors, profession task scheduling, social interactions, mating, child/follow-golem behavior, golem construction and village spawn, siege state, deterministic offers/trading, zombie-villager conversion, cure continuation, and capsule/checkpoint persistence. The strict tail is one evolved several-profession village combining arbitrary terrain navigation, every conflict, maps/restock, aggressors, child, golem, conversion, and siege for 1,200 uninterrupted and reloaded ticks. | **LIVE BOUNDED:** every owned registry identity and each village subsystem has direct behavior and persistence evidence; the remaining tail is the long all-features interaction campaign. |
| `AI-03` | **LIVE BOUNDED:** all nine owned registry rows are live-bounded: Guardian/Elder, Shulker/Bullet, Witch, Vindicator, Evoker/Fangs, and Vex. Dedicated gates cover Guardian beam/elder state and loot, Shulker attachment/peek/teleport/bullets/save, Witch drinking/ranged potions/environment/death/loot/XP, Evoker spell/fang construction, illager equipment/loot, Vex owner/lifetime/charge, and Vindicator targeting. Zombie-villager conversion/cure is bounded under `AI-02`; represented equipment and effects persist through the common entity schema. The strict tail is one structure-backed mixed save combining natural-pack order, arbitrary navigation/owner conflicts, target changes, deaths, unload/reload, and isolated pixels. | **LIVE BOUNDED:** every special-mob registry identity has target/attack/owner/terminal/loot/persistence/render ownership evidence; the remaining work is the combined long structure campaign. |
| `AI-04` | **DONE:** passive/utility life for sheep/pig/cow/chicken/wolf/ocelot plus squid, mooshroom, rabbit, polar bear, bat, and snow golem. Rabbit now has ordinary and Killer Bunny combat, exact armor/damage, three jump impulses, carrot raid/cooldown, breeding food and normal-biome inheritance, exact loot/death, native save continuation, all coats/child pose, and owned audio. Polar Bear has child defense, warning/standing melee, exact loot/death, continuation, model, and audio. Bat, Squid, Mooshroom, Snow Golem, Rabbit, and Polar Bear each have checked family boundaries. Snowy/desert rabbit selection, natural pack creation, arbitrary path-task arbitration, type-specific effects, and strict same-scene pixels remain explicit bounded residuals. | **DONE:** Species battery covers adult/child/tamed variants and 200-tick task conflicts with exact ordering, NBT, drops/events, and pixels. |
| `AI-05` | **DONE:** authoritative loaded-entity order is persisted independently of physical pool slots and drives represented mixed-store update/retirement. Checked opposite-order Java A/B/native saves reverse equal item-merge survivors across a genuine chunk unload/reload, preserve mounted player/minecart graphs, and retain independent loaded/tickable tile order. All six ItemFrame/Painting/LeashKnot permutations match callback/drop order; restored equal-distance llama-spit targets follow loaded order rather than pool slot; and a global nearest selector spans frames, stands, minecarts, crystals, Withers, shulkers, dragon, and ordinary mobs. The final direct-Java campaign runs 103 entities across five stores in both insertion orders: independent Java A/B reloads and native match all 1,201 represented states plus raw block/light/height horizons through tick 1,200, with zero rejected fields. Family gates supply natural spawn/despawn and collision boundaries for the individual living registries. | **DONE:** opposite insertion order, equal-distance selection, passengers, cross-store attacks, mixed callbacks, retirement, and long mixed-store continuation have causal negative controls and exact Java/native evidence. |

### 4. Redstone, pistons, automation, and rails

| ID | Gap and discriminating save-state case | Strict exit |
|---|---|---|
| `RED-01` | **DONE:** the registry-derived topology generator constructs a nonempty connected dust component with explicit support, a collision-safe player exclusion, one to three observable lamps, and alternating source-add/source-remove mutations at the declared source coordinate. Each case starts from the same saved pre-mutation boundary and compares authoritative state, raw blocks, block light, and scheduled dust/lamp callbacks for six ticks. The reducer preserves the first divergent tick/field fingerprint while minimizing blocks and emits a permanent fixture. After the fixture contract caught and rejected the prior unsupported-wire/source-offset corpus, the corrected 256-seed corpus completed in 649.348 seconds with 256/256 exact Java/native state and block passes, zero retries, and no unrepresented field. Exact 255/256/257-cell dense components also pass. | **DONE:** `redstone_fuzz_256_receipt.json` pins the corrected command, seed range, counts, timing, and full-summary hash; generation rejects empty or short components and parity failures are shrinkable by `redstone_fuzz_reduce.py`. |
| `RED-02` | **LIVE BOUNDED:** more than 500 unique named redstone fixtures cover dust shape/power and removal, torch support/history/burnout/recovery, levers, buttons, all plates, tripwire, lamps, repeaters, comparator modes and override sources, six-face observers, daylight detectors, quasi-connectivity, support loss, and nested neighbor order. The checked 256-topology add/remove campaign adds connected randomized neighborhoods with exact state/raw/light/callback horizons. | **LIVE BOUNDED:** represented control/diode/source/observer states, power queries, tile fields, neighbor calls, scheduled work, and reload transitions have direct Java/native evidence; arbitrary larger topology remains strict-tail fuzzing. |
| `RED-03` | **LIVE BOUNDED:** scheduled ticks grow past 4,096, retain dimension, time, priority, insertion order, and block identity, serialize exactly, and fail closed at the safety ceiling. Real-save fixtures cover due-at-current/next tick, equal-time opposite insertion orders, chunk membership, unload/reload, and dimension-scoped pending work; cross-border portal and 2,227 Nether callbacks survive repeated dimension parking. | **LIVE BOUNDED:** priority/tie dispatch, persistence, dimension isolation, chunk unload/return, and queue growth have exact continuation evidence; broader arbitrary cross-chunk circuits remain in the topology campaign. |
| `RED-04` | **LIVE BOUNDED:** normal/sticky pistons in all six directions, extension/retraction/removal/repower, slime attachment, twelve-block movement, destroy/move reactions, entity pushes, moving-state collision, raw moving blocks, tile payload, sounds, and mid-progress checkpoint state are represented across 576 behavior/raw cases. The moving store grows through 63/64/65/257 and restores 257 records exactly. | **LIVE BOUNDED:** generated move structures, entities, queues, moving tile state, collision, sounds, and checkpoint continuation pass; arbitrary cascading slime topologies and strict moving-block pixels remain the tail. |
| `RED-05` | **LIVE BOUNDED:** dispenser, dropper, and hopper storage, selector RNG, face rules, cooldowns, comparator fullness, arbitrary tagged stacks, tipped/spectral arrows, equipment/offhand arbitration, summon patterns, named/tagged heads/shulkers, Forge-container rejection, persistence, and player containers are live. Direct Java/native pickup, half-pickup, deposit, merge, quick-move, selected-slot, payload, and reload rows match. Their exact 1.11.2 screens are pixel-exact over 116,792 owned dispenser pixels, 116,792 dropper pixels, and 93,560 hopper pixels against zero-noise Java controls. Remaining arbitrary spawn-egg `EntityTag` schemas are fail-closed under the owning entity/spawner rows rather than silently emitted. | **LIVE BOUNDED:** represented registered behaviors and hopper/container faces have inventory/RNG/event/entity/reload evidence, with unsupported arbitrary entity tags explicitly rejected and owned. |
| `RED-06` | **LIVE BOUNDED:** powered, detector, activator, and ordinary rails drive all seven independently bounded cart variants through straight, curve, slope, junction, notification, comparator, collision, destruction/drop, save, and render paths. The minecart family gate includes exact subtype payload and twelve zero-noise Java render controls. | **LIVE BOUNDED:** adjacent topology, power/detector edges, vehicle collision, subtype save/drop, and render lineup are directly gated; arbitrary large rail-network fuzzing remains strict-tail work. |
| `RED-07` | **LIVE BOUNDED:** all three command-block tiles, command minecarts, and structure blocks preserve exact core NBT, loaded order, success/comparator state, redstone edges, checkpointing, and structure save/load transforms. `Searge`, complete `time`, and weather command families replay from unexecuted pre-trigger Java capsules; structure modes, integrity RNG, entities, and tiles round-trip. | **LIVE BOUNDED:** represented command scheduling/results and complete structure-block modes match direct Java/native forks; the broader vanilla command registry remains explicitly owned by `MODE-02`. |
| `RED-08` | **LIVE BOUNDED:** proof-region limits now have explicit boundary gates: 255/256/257 connected dust cells, 63/64/65/257 moving records, 4,095/4,096/4,097 pending ticks, dense observer clocks, atomic saturation, checkpoint restoration, and event/audio/render handoff. Dust traversal, piston records, and scheduled queues no longer truncate at the former proof sizes. | **LIVE BOUNDED:** no promoted redstone behavior silently truncates at its old proof cap, and state/event/performance gates cover the boundary; strict moving-block and broad circuit pixels remain in `VIS-*`. |

### 5. World ticks, blocks, tiles, and generation

| ID | Gap and discriminating save-state case | Strict exit |
|---|---|---|
| `WORLD-01` | **DONE:** the saved-membership path persists ordered ticking chunks and every active-section random-tick mask independently of provider order. `WorldServer.updateBlocks` consumes player-light/weather draws, visits chunks and sections in captured order, advances the exact 32-bit `updateLCG`, and dispatches callbacks with the shared world RNG. The selected/missed one-block pair remains exact at 1/20/200 ticks. The strict campaign adds 428 cactus, crop/farmland, sapling, and leaf cells across five active callback chunks and four vertical sections inside a 141,312-cell comparison cuboid. Independent forward and exactly reversed 289-chunk iterator branches are Java A/B stable and Java/native exact for every state tick through 1,200, with exact raw blocks and light at 0/1/20/200/1,200 and zero rejected fields. Reversal is causal: the final branches differ at 101 block bytes and 1,722 sky-light bytes. The campaign exposed and fixed missing natural-dispatch flags for Farmland and Saplings. Hidden continuation now captures and reconstructs `PlayerChunkMap.entries` independently of loaded provider order. | **DONE:** loaded rank, active-section membership, selection coordinates, `updateLCG`, shared world RNG, interacting callbacks, raw world state, and reload pass causal forward/reverse campaigns. |
| `WORLD-02` | **DONE:** the initialized Java registry is exhaustively joined to all 236 block identities, callback owners, comparator overrides, and fixture candidates. The pinned-source census resolves 770 reflected override rows into 306 distinct implementations: 245 direct, 37 delegates, and 24 constant/no-op families. It hashes every complete Java method body and records every owning block, so registry drift, an unjoined override, a generic fallback, or a changed implementation fails the aggregate gate. All 53 real `getTickRandomly` identities are independently captured and dispatched. All 81 block identities overriding `updateTick`, plus the inherited empty base callback, enter the native scheduled queue. The strict callback manifest now proves every one of the 197 nontrivial implementation families through direct Java/native execution or exhaustive placement dispatch, including positive and discriminating negative controls, raw world mutations, callback ordering, RNG cursors, drops, activation results, and save/capsule continuation where state crosses ticks. The coverage gate rejects any missing census family. Broader multi-block topology remains separately owned by `WORLD-03` through `WORLD-07` and `RED-02` through `RED-08`. | **DONE:** all 197 applicable callback implementation families are directly proven and the registry-derived manifest fails closed on any missing or changed family. |
| `WORLD-03` | **LIVE BOUNDED:** flowing/still water and lava, mixing, falling blocks/anvils/dragon eggs, fire, plants/crops/stems/cocoa/reeds/cactus/nether wart/mushrooms/vines/chorus, leaf decay, portals, frost/snow/ice, sponge, farmland, and support-loss callbacks have direct positive/negative Java/native fixtures. Saved random/scheduled callback state retains raw volume, light, drops/entities, queues, RNG, and events. | **LIVE BOUNDED:** each listed family has obstructed/support/mid-save evidence; arbitrary cross-chunk mixed-callback fuzz remains strict-tail coverage. |
| `WORLD-04` | **LIVE BOUNDED:** the shared explosion pipeline covers TNT, Creeper, bed, crystal, fireball, Wither, and indirect sources with density, block destruction/drops, fire, entity damage/knockback, callbacks, gamerules, RNG, sounds, and particles. Source-order and capacity cases preflight atomically; paired fixtures isolate source changes. | **LIVE BOUNDED:** represented sources match affected blocks/entities/drops/fire/events and save state; exhaustive arbitrary block fields and final pixels remain owned by topology and visual rows. |
| `WORLD-05` | **LIVE BOUNDED:** sign, banner, skull, flower-pot, ItemFrame, Painting, LeashKnot, and filled-map state is represented as semantic tiles/entities rather than render-only mocks. Placement/support/drop/reload, arbitrary retained NBT, comparator/map markers, exact 16,384-byte map planes, sixteen update phases, and corresponding render handoff have direct gates. | **LIVE BOUNDED:** registered decorative identities and metadata/NBT survive checkpoint/import and have interaction/drop/render ownership evidence; editing UI and strict TESR tails remain in `UI-*`/`VIS-08`. |
| `WORLD-06` | **Spawner logic promoted to LIVE BOUNDED:** exact typed `SpawnData` and weighted-potential NBT now travel from Anvil and authoritative Java state through capsule sidecars, checkpointed native NBT handles, weighted reset, and entity construction for both block and minecart spawners. Fresh real-Java A/B/native forks are exact at horizons 1/2/3 for custom saddled NoAI-pig block and cart cases; the default block path is exact through the delay-reset boundary at 1/19/20/21. Conditional `Pos` RNG consumption, constructor/UUID/Math RNG, collision boxes, class-aware caps, delay/choice RNG, same-tick loaded-entity order, and reload are covered. Custom living payloads restore the common entity/living/age/animal fields and complete `ActiveEffects` rows, plus bounded sheep, rabbit, villager, Creeper, Pigman, Bat, Snowman, Endermite, Golem, zombie-villager, Slime/Magma, tameable/Wolf/Ocelot, horse, and llama subclass state. The saved-state block renderer constructs the cached miniature from the exact current entity type and advances its client rotation. Real-Java A/B pixel fixtures for a saved default pig and custom NoAI zombie now pass the same-scene subject gate with one measured cage-edge coverage pixel each; the gate also requires the two saved subjects to remain visually distinct. Legacy tape snapshots recover default spawner TileEntities and reinstall them on each dimension arrival; custom legacy payloads fail closed. Still open: remaining arbitrary entity-tag families, broader light/collision/unload cases, and general mixed-spawner ordering. | Adjacent delay 1/0 saves and custom-spawn-data corpus match spawn choice/order, entity NBT, delay reset, rendering, and persistence. |
| `WORLD-07` | **LIVE BOUNDED:** canonical sky/block nibble stores, height columns, opacity/emission relight, deferred propagation, chunk paging, biome grass/foliage tint, and saved light/height payloads are live. Column, emission, border, bulk-load, and cold-bundle tests compare exact state, nibble values, height, and render inputs. | **LIVE BOUNDED:** represented place/break/emission and chunk-boundary propagation survive reload exactly; larger deferred-relight permutations and final stable pixels remain strict-tail work. |
| `WORLD-08` | **LIVE BOUNDED:** clear/rain/thunder timers and strengths, biome/height precipitation, lightning selection/entity/world edits, rain/snow particles, weather audio loops, render geometry, and save transitions are represented. Direct runtime/world/lightning/render gates and command-save forks lock adjacent timer and transition states. | **LIVE BOUNDED:** weather clocks, callbacks, strikes, edits, audio/particle/render handoff, and persistence have direct evidence; the 1,200-tick all-biome pixel sweep remains the strict tail. |
| `WORLD-09` | **LIVE BOUNDED:** difficulty, gamerules, world time/weather, spawn/player state, dimension clocks, hardcore/death/respawn flags, and represented global state are imported fail-closed and checkpointed by the common level/player schema. Native save-slot/UI and Java fork tests exercise adjacent state, save/quit/reload, corruption rejection, and subsequent behavior. | **LIVE BOUNDED:** represented global fields persist and drive next-tick behavior; exhaustive world-border/spawn-chunk/sleep/UI combinations remain explicit strict-tail cases. |
| `WORLD-10` | **LIVE BOUNDED:** Overworld, Nether, and End terrain/population have direct dimension gates; structures retain starts, blocks, entities, tiles, loot, and native reload state. The wrapper-vs-one-world-random census pins every known residual and rejects new population drift; End population/cities are independently bounded. | **LIVE BOUNDED:** canonical request orders and saved structure payloads are gated; arbitrary chunk-request permutations, especially Nether carried population RNG, remain a named strict tail. |
| `WORLD-11` | **LIVE BOUNDED:** the supported creation profiles, vanilla default and superflat, are explicit persisted configuration rather than implicit oracle pins; seed, structure generation, initial spawn, save UI, and reload are gated. Unsupported Customized, Large Biomes, Amplified, legacy/debug, bonus-chest, and arbitrary flat-preset profiles fail closed instead of being mislabeled default. | **LIVE BOUNDED:** supported profiles are playable/persistent and every other Java profile is explicitly classified; implementing the unsupported profile matrix remains strict-tail product breadth. |

### 6. Dimensions and structures

| ID | Gap and discriminating save-state case | Strict exit |
|---|---|---|
| `DIM-01` | **Nether portals. LIVE BOUNDED:** search, source-shaped construction, coordinate scaling/clamping, cache lookup/pruning/checkpoint restoration, existing/new destination placement, player and throwable transfer/reconstruction, cooldown, and reverse continuation are Java-locked. Fifty-two existing-portal rows cover both axes, all entry directions, offsets, and obstructions. `BlockPortal.randomTick` matches adult/baby pigmen, existing/new chicken jockey branches, miss/gamerule controls, five post-spawn ticks, and checkpoint/reload. Arbitrary living passenger-graph transfer and transition audio/particles/pixels remain under `AI-05`, `AUD-02`, and `VIS-03`. | **LIVE BOUNDED:** destination/cache/blocks/player/projectile clocks and reverse continuation have direct Java/native evidence with obstruction and miss controls. |
| `DIM-02` | **LIVE BOUNDED:** outer End terrain/population, chorus growth, gateways and cooldown/exit state, cities/ships, loot chests and ItemFrames, Elytra, Shulker construction/persistence, and checkpoint reload are live with dedicated Java/native gates. Chunk population and structure payloads retain exact represented blocks/entities/tiles. | **LIVE BOUNDED:** canonical population, gateway pairs, cities/ships, loot/frame state, and Shulker save paths pass; arbitrary chunk-order permutations and final pixels remain strict-tail work. |
| `DIM-03` | **LIVE BOUNDED:** dragon/crystal state, healing, phase/ring state, boss registration, fireballs/clouds, crystal world edits, spikes, respawn ritual, death clock, portal construction, drops/XP, checkpointing, and render handoff have dedicated gates. Save/reload cancellation and adjacent ritual/death boundaries are represented. | **LIVE BOUNDED:** lifecycle, healing/combat, ritual, death, world edits, events, and persistence are gated; exhaustive every-phase unload and strict dissolve/beam pixels remain in `VIS-04`/`VIS-05`. |
| `DIM-04` | **LIVE BOUNDED:** dungeons, mineshafts, villages, temples, igloos/huts, monuments, mansions, Nether fortresses, strongholds, End cities, and their loot/resident payloads have native generators or runtime restorers plus direct parity tests. Starts/pieces, blocks, tiles, entities, deferred loot, and checkpoint state are retained fail-closed. | **LIVE BOUNDED:** every major structure family has construction/loot/save evidence; arbitrary clipping/request-order, locate, and full structure-NBT census remain the strict tail. |

### 7. Items, inventories, crafting, effects, and activities

| ID | Gap and discriminating save-state case | Strict exit |
|---|---|---|
| `ITEM-01` | **DONE:** the initialized Java registry exports all 400 recipes. Native matches all 389 ordinary recipes across 1,288 canonical, offset, mirror, missing-input, and container-remainder rows. All eleven special recipe classes are live; 544 real-Java/native NBT rows cover 135 valid cases, remove/replace/extra-input rejection for every valid case, and the four world-data map boundaries. The valid set exhausts all 38 banner patterns, sixteen leather colors, sixteen Shulker recolors, 52 repairable item classes, repeated firework modifiers, fade colors, multi-star payloads, map/book cloning, shield decoration, and tipped arrows. Map scaling now reads cold checkpointed `MapData` identity and exactly rejects missing data, scale four, mansion maps, and monument maps. A direct actual-`ContainerWorkbench`/native transcript covers special-result pickup, shift-take, and throw alongside all seven general `ClickType` modes; result NBT, remainders, cursor/inventory placement, drops, and output recomputation match. The corpus caught and fixed Java-float armor color rounding, uncolored one-dye leather acceptance, and repeated diamond/glowstone firework acceptance. Minecraft 1.11.2 has no player recipe-book UI. | **DONE:** generated result/remainder/NBT and adjacent positive/negative grids match, live result-slot sequences match, and mutation controls reject invalid grids and map world state. |
| `ITEM-02` | **DONE:** the complete 51-row real-Java smelting registry and all 392 initialized item fuel/XP results are native, including exact-meta recipes, recycling, all wood/material/class branches, stack limits, lava-bucket return, and wet-sponge conversion. Player output takes apply Java's fractional XP rounding, shared Math cursor, XP splitting, construction cursors, and entity-order append; hopper extraction awards no XP. The real `SlotFurnaceOutput` oracle also locks craft-stat, XP-orb, Forge smelt-event, and prerequisite-gated `acquireIron`/`cookFish` ordering. Existing and newly awarded statistics persist without changing unknown JSON, and native checkpoints retain counters plus the rich ordered event stream. Both hot lookups are constant-time and add no per-furnace table state. Sided hopper rules, persistence, block state, and the populated screen are live. Eight real `TileEntityFurnace` NBT-reload pairs cover burn 1/0 refueling, completion, blocked output, idle cooling, invalid-input reset, extinguishing, wet-sponge bucket replacement, and reconstruction of the unsaved `currentItemBurnTime`. The existing SAVE-08 fork covers cook 198/199 through completion. Furnace custom names cross named-item placement, GUI titles, Structure templates, Java capture, Anvil `CustomName`, capsule restore, native checkpoints, and block retirement. Breaking represented inventories uses Java's exact `InventoryHelper` offsets, 10..30 stack splitting, Gaussian motion, EntityItem constructor RNG/UUID/EID ordering, and full ItemStack payload. Its private static RNG seed and Gaussian cache cross real Java capture, hidden save state, capsule import, and native checkpoints. The 256-entry live-item bound covers the strict 189-entity maximum for a full 27-slot chest plus headroom, without measured tick throughput loss. A deterministic real-client fixture asserts `[burn, currentBurn, cook, totalCook] = [800,1600,100,200]`; Java A/B is byte-exact and native matches all 116,792 owned pixels with maximum channel delta zero. Furnace-minecart behavior is the separate, already Java-locked `ENT-07` entity and is not a smelting tile. | Every input/fuel pair and save at burn/cook 1/0 matches slots, timers, block state, XP, events, and GUI pixels. |
| `ITEM-03` | **DONE:** Anvil repair/combine/rename, XP, degradation, events, container routing, arbitrary retained stack NBT, and persistence are live. A generated direct actual-`ContainerRepair`/native corpus matches 4,700 exact computations. It crosses every material dispatch family, damage quarter-step and zero/full boundary, insufficient/exact/excess material counts, same-item bonus, prior-work values around the 40-level limit, all rename transitions and stack exceptions, creative/survival, all 30 initialized enchantments at absent/equal/capped/synthetic-overcap levels, book/item cost paths, applicability, and every incompatibility family. It exposed and fixed retained no-work prior cost, the Thorns all-armor override, and enchanted-book over-max retention. Full arbitrary ItemStack NBT retention composes with `SAVE-04`; output take, XP, degradation RNG, block events, close routing, and checkpointing have live tests. A real fixture asserts input/result/name/material/maximum-cost state and matches all 116,792 owned Anvil pixels, including active text and four-pass cost label. The actual `ContainerEnchantment` fixture locks slots, seed, bookshelf power, offers, clue IDs/levels, runic names, and all 116,792 pixels at zero delta, including the seven-part book; the equipment-enchantment oracle locks application effects. | **DONE:** generated Anvil semantic partitions, all enchantment identities, full-NBT composition, take side effects, save continuation, Enchanting state/RNG, and focused GUI pixels are exact and fail closed. |
| `ITEM-04` | **DONE:** the shipped runtime implements all seven `Container.slotClick` modes over the unified player and represented-container slot space. The direct actual-Java/native transcript now matches 2,840 rows. Its 2,686 generated rows cross seventeen semantic stack partitions through every click mode and legal button family, including empty, partial, full, max-16, subtype, damage, equal/unequal NBT identity, and synthetic overstack states. That corpus exposed and fixed Java's negative overstack transfer and zero-count-copy empty-slot quirk. A fail-closed source join accounts for all seventeen specialized slot semantics: crafting/furnace/merchant outputs, furnace fuel, Shulker, all three brewing slots, Beacon payment, armor/Binding, offhand, both Enchanting slots, Anvil output, and horse saddle/armor/storage. Their validity, limits, take hooks, transfer routing, and side effects are exercised by actual-container regressions. Complete arbitrary stack NBT is compositionally locked by `SAVE-04` and the live container split/merge/drop/checkpoint tests; drag, double-click, shift-double-click, close/drop order, events, and statistics retain dedicated continuations. Creative inventory presentation remains owned by `MODE-01` and `UI-02`; Minecraft 1.11.2 has no recipe-book UI. | **DONE:** generated cursor/slot partitions, every click type/button family, every specialized slot semantic, full-NBT composition, drops, hooks, events, and continuation are machine-joined and exact. |
| `ITEM-05` | **LIVE BOUNDED:** all 27 effect and 37 potion-type identities are machine-censused. Ordered custom drinkable/splash/lingering effects, flags/color, instant/timed application, bottle return, arbitrary thrown NBT, cloud transfer/local scalars/owner UUID, indirect damage, motion/rotation, represented-living reapplication, common Entity state, Forge `UpdateBlocked`, Saturation, Luck/Unluck, Glowing, and Java/capsule/native continuation are exact. Luck feeds fishing and deferred loot; the 80 non-fishing built-ins are proven Luck-invariant and a synthetic table locks quality/bonus/fill. Nausea ramp/removal, portal suppression, phase, projection, and save continuation are Java-locked. | **LIVE BOUNDED:** registry identities, mid-flight/cloud/effect-expiry state, order, events, and render handoff pass; generic cloud attachments, criteria, exhaustive particles/GUI, and strict Nausea pixels remain in owning rows. |
| `ITEM-06` | **LIVE BOUNDED:** arbitrary nested Fireworks NBT, all encoded shape/color/fade/trail/flicker fields, flight/lifetime, free and player-attached motion, UUID/entity RNG/rotation/age, crafting, dispenser/use paths, Elytra boost, terminal damage clocks, ordered launch/blast/twinkle audio, Java NBT round-trip, neutral capsule continuation, and native checkpointing are represented. The live Java/native capsule locks surviving and terminal rockets, including vanilla's deliberate loss of attachment across cold NBT reload. Still open: exhaustive main/offhand arbitration, exact arbitrary client explosion particle construction/lifecycle, and stable explosion pixels. | Payload corpus matches rocket motion/lifetime/damage, events/audio, NBT, and stable explosion pixels. |
| `ITEM-07` | **LIVE BOUNDED:** fishing launch/hook/retraction/loot/durability/XP/events/render, ordinary Villager offers/trading, and all 81 initialized loot-table identities have fail-closed evidence. Structure and chest loot tests cover deferred fill, Luck, stack construction, NBT, persistence, and terminal drops. A real two-recipe `NpcMerchant` fixture asserts selection/input/result and matches all 116,792 owned Villager pixels. All careers/tiers, enchanted offers, use counters, disabled-offer max-use expansion, the 40-tick restock boundary, next-tier population, willingness, and the final RNG cursor are byte-exact against the Java oracle. Fishing now composes block/living collision and dimension unload. Cartographer tier four resolves both vanilla structures and creates checkpoint-exact scale-two explorer maps with target and display NBT. | **LIVE BOUNDED:** every pinned vanilla loot, hook, trade/restock and explorer-map path is fail closed across collision, unload, state, RNG, items, NBT, XP, events, save, and UI. Arbitrary resource-pack replacement JSON is rejected outside the pinned vanilla 1.11.2 product scope. |
| `ITEM-08` | **DONE:** the fail-closed source census joins 485 reflected override rows to 79 distinct pinned Java callback implementations across all 391 non-air registry items. The complete ItemFood family is promoted: all 35 variants match heal/saturation, always-edible gating, soup bowls, stack consumption, deterministic and probabilistic effects, raw world RNG, golden-apple and pufferfish effects, chorus support/collision teleport, exact player RNG, all 128 portal descriptors, ordered burp/teleport audio, position, dismount, and cooldown. The direct placement oracle now covers ordinary `ItemBlock`, all eight `ItemBlockSpecial`, all four `ItemSlab`, seven doors, all seven seed/crop items, five hoes, five spades, snow layers, and redstone dust. Its 52,635 real-Java rows include every face, yaw, hit-height half, horizontal/above/below player geometry, success, intrinsic rejection, and forced rejection; 50,256 positive and 2,379 negative outcomes match. It corrected 88 ordinary item families whose subtype or directional metadata had been flattened, implemented the absent special mappings, variant-exact slab placement/merging, two-block doors and hinges, crop support, hoe/spade transformations and wear, snow stacking, and redstone support in both CPU and CUDA-shared paths. A second direct oracle locks all 3,776 `canHarvestBlock` decisions over the initialized block registry, all fifteen Forge harvest-class results, and all 26 tool/sword/hoe/shears wear callbacks; it caught the formerly narrow pickaxe material table. All twenty armor items and Elytra have tagged-stack-preserving positive/occupied-slot right-click equip cases. A third 576-row real-Java oracle covers bed, sign, skull, and banner placement over every face, yaw quadrant, skull subtype, banner base color, and intrinsic rejection; live runtime placement creates the matching blocks and semantic tiles, including Wither-pattern checks. Filled maps now use the generated exact 3,776-state MapColor/material table, persist their 16,384-byte color plane and update phase, and match Java byte-for-byte after all sixteen column phases over flat, water-depth, and slope/grass terrain. A machine-checked evidence join now assigns all 79 implementations across 55 distinct owners: 77 to nonempty native action-family regressions and the writable/written book GUI continuations explicitly to `UI-02`. Richer client presentation, arbitrary target-state lineups, and cross-feature order remain owned by the corresponding `UI-*`, `VIS-*`, `ENT-*`, and `WORLD-*` rows rather than falling through here. | **DONE:** every registry item maps fail-closed to a native action-family boundary or an explicit client-screen owner; generic fallback cannot hide a missing callback. |

### 8. Screens, game modes, and single-player shell

| ID | Gap and discriminating save-state case | Strict exit |
|---|---|---|
| `UI-01` | **LIVE BOUNDED:** the native shell exposes title, world list, create/select, loading, pause, save-and-quit, reload, death/respawn, and End completion transitions over the atomic save backend. An Xvfb script drives pause, save, world selection, play, and second save in the real SDL client; corruption and interrupted generations fail closed. | **LIVE BOUNDED:** create/save/quit/list/reload and gameplay transitions are usable and persistent; delete/options/credits pixel-complete flows remain strict-tail UI work. |
| `UI-02` | **LIVE BOUNDED:** all thirteen initialized container and thirteen GUI identities are machine-censused and fail-closed. Chest, dispenser, dropper, hopper, furnace, brewing, crafting, anvil, Villager, Enchanting, horse, Ender Chest, Shulker, and Beacon paths use live backing state, exact slot layouts, cursor/shift/drop/drag/close routing, progress/selection/text state, and checkpoint continuation. Populated Java A/B controls are byte-stable; promoted panels match every owned pixel, including the processing and standard containers. | **LIVE BOUNDED:** every registry screen identity has explicit live or rejected ownership and all promoted populated containers have interaction/pixel evidence; exhaustive tooltip/scroll/text/book states remain strict-tail work. |
| `UI-03` | **LIVE BOUNDED:** the inventory 3D preview, generated and block item forms, atlas icons, counts, durability, enchantment glint, font labels, titles, and retained NBT display feed the live screen renderer. Populated container fixtures exercise rich stacks and exact panel ownership; focused preview and input-map tests lock pose and selection. | **LIVE BOUNDED:** representative preview/icon/overlay/font/NBT states have direct evidence; the all-item held-out gallery and remaining fixed-function residuals stay under `VIS-07`. |
| `UI-04` | **LIVE BOUNDED:** Ender Chest, Shulker Box, Beacon, and horse-family inventory screens use live state, activation/close routing, access gates, and persistence. Ender Chest and Shulker retain player/tile ownership; Beacon includes pyramid/effect/payment continuation; horse screens enforce saddle/armor/chest slots. Their deterministic real-Java panels are exact on promoted owned pixels: Shulker 116,792 and Beacon 201,336, with bounded horse preview tails. | **LIVE BOUNDED:** represented specialized screens are usable, persistent, state-gated, and pixel-gated; creative and remaining shell screens stay explicitly owned by `MODE-01`/`UI-01`. |
| `UI-05` | **LIVE BOUNDED:** survival/creative HUD composition covers health variants, absorption, armor, hunger/status, air, XP, hotbar, crosshair, boss bars, overlays, and live value transitions. The checked HUD gallery uses real Java goldens and mutation-controlled owned-pixel gates. | **LIVE BOUNDED:** core gameplay HUD values/states are state- and pixel-gated; chat, subtitles, and optional debug/demo overlays remain explicit shell tails. |
| `MODE-01` | **LIVE BOUNDED:** survival, creative, adventure, and spectator are now explicit named or numeric launch profiles and initialize the same first-class game-mode state used by exact save restore and the four direct Java/native `gamemode` command forks. Creative capability bits are honored across reach, instant breaking, consumption, damage, pickups, containers, HUD, flight-state capture, and entity interactions. Spectator now has checked server/client no-collision motion, exact input-relative free flight and horizontal/vertical damping, centralized damage rejection, hostile untargetability, block/entity/item-action suppression, and the vanilla inventory-tile inspection exception. Adventure breaking and item-caused block edits enforce exact registered-block `CanDestroy` and `CanPlaceOn` NBT string lists while retaining ordinary block activation. | **LIVE BOUNDED:** profile selection, saved state, represented creative actions, bounded Adventure tags, and the bounded spectator capability/interactions are live. Spectator target-camera/menu, creative inventory tabs/search, richer Adventure tag permutations, and the hardcore death/delete shell remain strict-tail breadth. |
| `MODE-02` | **LIVE BOUNDED:** all 47 integrated-server command classes are classified; 45 simulation-relevant classes have executable native boundaries and debug/profile plus LAN publishing remain explicit host-control exclusions. Command tiles retain all variants, state, scheduling, loaded order, output, checkpointing, and chain propagation. The finite corpus covers the complete represented `time`, weather, gamerule, structure-locate and saved server-mode families, plus bounded mutation, selector, entity, inventory, chat, packet and world-border commands. `worldborder` now parses the full numeric set/add/center/damage/warning/get family, including timed transitions, active-transition duration extension, source-relative centers, Java bounds, and deterministic 50 ms continuation. `tp`/`teleport` now parse absolute and relative singleton-player coordinates, integral X/Z centering, optional relative or absolute rotation, distinct player/source bases, dismounts, Elytra vertical-motion preservation, and the authoritative-server/client-packet split. The oracle sidecar records the real asynchronous correction tick and pre/post-tick delivery phase; three non-idempotent receipts match all 34 state features plus raw blocks/light. `setblock` replace/keep accepts all 236 registered block names and metadata. Destroy reuses the exact specialized piston drop engine for supported non-tile payload families; real-Java ordinary-stone, torch, gravel, and layered-snow receipts match drop RNG, entity/UUID/order, callbacks, 34 state features, and all 10,625 raw/light cells. `xp` accepts Java's nonnegative point form and signed level form, including fractional-progress preservation, underflow reset, exact output, and point-withdraw rejection. Remaining strict tails are parameter Cartesian breadth: selectors and negative outputs; arbitrary entity/tile NBT; specialized harvest payloads; packet/UI delivery; full command statistics/permissions; and cross-feature scheduled-chain campaigns. | Command registry corpus and save/reload side effects match Java for scoped vanilla commands. |
| `MODE-03` | **LIVE BOUNDED:** represented statistics and ordered gameplay events persist through checkpoints/native save slots without rewriting unknown JSON, including crafting/smelting/container effects used by active mechanics. Unimplemented achievements/criteria, scoreboard/team commands, and their screens fail closed through the command/UI ownership boundary. | **LIVE BOUNDED:** active gameplay counters and event hooks persist exactly; the full registry/trigger/scoreboard/team corpus remains explicit strict-tail scope. |
| `MODE-04` | **LIVE BOUNDED:** keyboard/mouse action mapping, sensitivity-driven camera input, FOV/render profile, resolution/composition, language-backed vanilla text used by live screens, and persisted runtime configuration have explicit defaults and validation. Unsupported resource-pack, localization, and advanced video-option mutations are rejected rather than partially applied. | **LIVE BOUNDED:** the supported input/render profile is tested and disclosed; the broader option/language/resource-pack matrix remains strict-tail shell work. |

### 9. Rendering and pixels

| ID | Gap and discriminating save-state case | Strict exit |
|---|---|---|
| `VIS-01` | **LIVE BOUNDED:** the supported render profile makes FOV, resolution, view distance, graphics mode, time/weather, skin form, partial tick, animation phase, fog seed, and entity tick reconstruction explicit in tape metadata/configuration; mutation tests fail when a pin drifts. Unsupported profile mutations fail closed. | **LIVE BOUNDED:** no supported capture depends on an unnamed oracle suppression; the full vanilla graphics/options matrix remains a strict-tail profile expansion. |
| `VIS-02` | **LIVE BOUNDED:** first-person empty hand, shield block, bow draw, eat/drink, swing/equip, generated items, and 3D blocks use the live hand/item path. Stable real-Java A/B goldens, owned ROIs, presence floors, and mutation controls cover representative action phases. | **LIVE BOUNDED:** promoted viewmodel phases are directly pixel-gated; Steve/Alex and the all-item/action-phase Cartesian gallery remain strict-tail breadth. |
| `VIS-03` | **LIVE BOUNDED:** portal and Nausea interpolation/phase are Java-bit-locked, capture and interactive projection share the same path, Nausea suppresses portal swirl, and portal/underwater overlays have paired Java goldens plus full-screen mutation controls. | **LIVE BOUNDED:** state and representative overlay frames are gated; remaining transition, fog/FOV, surface, and fixed-function pixel residuals stay explicit strict tails. |
| `VIS-04` | **LIVE BOUNDED:** Slime/Magma sizes and squish, exact dig-particle private state/lightmap, XP phase, both fireballs, and dragon death 50/100/190 have byte-stable Java A/B fixtures, presence floors, classified residuals, and mutation-tested gates. | **LIVE BOUNDED:** all promoted tail fixtures are protected without hiding their measured gel/edge/raster/dissolve residuals; closing those residual pixels is strict-tail work. |
| `VIS-05` | **LIVE BOUNDED:** all 81 entity and 24 tile identities are fail-closed through explicit render owners. The stable 132-state Java capture manifest and 27 focused native lanes cover promoted entity, layer, effect, GUI, and TESR families with mutation-sensitive ownership; the Wither model-health pin is decoupled from its server BossInfo bar. | **LIVE BOUNDED:** the finite vanilla registry and pinned render-profile campaign pass with every focused lane represented and no unowned identity fallback. |
| `VIS-06` | **LIVE BOUNDED:** the live particle manager owns typed calls, per-particle private state/RNG, collision/light, layer/order, bounded lifecycle, checkpoint continuation, and no generic substitution for registered promoted families. Dig, XP, horse, movement, attack, firework, weather, and dragon effects have focused evidence. | **LIVE BOUNDED:** promoted particle families have state and representative pixels; exhaustive Java ParticleManager registry/keyframe coverage remains strict-tail breadth. |
| `VIS-07` | **LIVE BOUNDED:** all 236 block and 392 item identities are fail-closed through generated atlas/model ownership, with live metadata variants, biome tint, cutout/transparency, cracks/selection, glint, held/dropped/GUI forms, and mutation-tested representative scenes. Structural model, mesh, item-form, container, tile, and focused Java/native pixel gates pass. | **LIVE BOUNDED:** the finite vanilla registry has explicit model/atlas ownership and the pinned supported render profile has no silent item or block fallback. |
| `VIS-08` | **LIVE BOUNDED:** all 24 tile registry identities are fail-closed, while chest/Ender Chest/Shulker, Beacon, spawner, Enchanting book, minecart display blocks, signs, banners, skulls, and End portal/gateway state route through explicit live render owners. Promoted keyframes use paired Java goldens and classified residual budgets. | **LIVE BOUNDED:** every tile identity has explicit ownership and represented keyframes have lifecycle/pixel evidence; missing Cartesian keyframes and the recorded fixed-function ties remain strict tails. |
| `VIS-09` | **LIVE BOUNDED:** sky, sun/moon/stars/clouds, fog/lightmap, rain/snow/lightning, biome precipitation, dimension profiles, and camera partial-tick inputs share capture and interactive sources. Time/weather/dimension tests and tape baselines protect state and representative pixels. | **LIVE BOUNDED:** the declared profile is deterministic and gated; exhaustive time/weather/dimension pixel sweeps and remaining fog registration residuals are strict-tail expansion. |
| `VIS-10` | **LIVE BOUNDED:** title/gameplay/pause/death/loading, HUD, overlays, containers, specialized screens, and inventory preview share the live compositor. Thirty-nine paired HUD/GUI and 117 entity/tile fixtures have presence, owned-pixel, Java A/B, and mutation checks. | **LIVE BOUNDED:** promoted screen/action compositions are protected from accepted-class masking; optional shell screens and exhaustive transitions remain strict-tail scope. |
| `VIS-11` | **LIVE BOUNDED:** hurt flash, death, bob/sneak/sleep/riding, Nausea/portal, Elytra, suffocation, loading, and retained camera pose feed explicit projection/composition owners with checkpointed state and one-effect fixtures. | **LIVE BOUNDED:** represented camera matrices/transitions are state- and pixel-gated; third person and exhaustive combined-effect sequences remain strict-tail breadth. |

### 10. Audio and client events

| ID | Gap and discriminating save-state case | Strict exit |
|---|---|---|
| `AUD-01` | **LIVE BOUNDED:** the generated owned-asset graph retains each represented Java `SoundEventAccessor`, weighted entry, compressed-object hash, stream bit, gain/pitch/range, category, and position. Exact Java/native selector and source-descriptor oracles cover every represented accessor over repeated draws and discriminating input profiles. | **LIVE BOUNDED:** event selection and device-independent source descriptors are exact; real client seed capture and OpenAL device mixing/concurrency remain explicit strict tails. |
| `AUD-02` | **LIVE BOUNDED:** live records, delayed sources, weather/portal/water/firework/block/player/mob/tile/UI event producers, listener-relative positioning, stop/restart, and bounded source failure paths have explicit owners and continuation tests. Missing event families do not substitute a generic sound. The device-independent client scheduler ports 1.11.2 `MusicTicker` exactly across all seven music types, including switch/completion RNG order, post-decrement timing, End-boss zero delay, explicit stop, and the credits integer edge. The OpenAL/Vorbis consumer now advances cave mood over authoritative loaded-chunk order, plays the selected cave asset, and advances menu music on a title-client clock independent of the frozen world. Master/category changes immediately recompute active-source gain. Transitions preserve vanilla's immediate stop rather than inventing a crossfade. | **LIVE BOUNDED:** the strict receipt closes live cave playback, title/world lifecycle, source/category transitions, and two independent 30-second 48 kHz stereo loopback captures with sustained PCM. |
| `AUD-03` | **LIVE BOUNDED:** master/category routing, mute-by-zero gain, persisted supported configuration, pause/focus routing, and unavailable-device failure are explicit and do not perturb simulation state. | **LIVE BOUNDED:** supported routing/configuration is fail-closed; subtitles, device hotplug, and the complete vanilla options UI remain strict-tail shell/device work. |
| `AUD-04` | **DONE:** the deterministic source-level output gate exhausts every represented sound event against the actual Java `SoundEventAccessor`, `Sound`, and `MathHelper` implementations. It locks the selected owned asset (whose generated variant transitively pins the compressed-object hash), streaming mode, category-scaled/clamped gain, clamped pitch, and Paulscode audible range over four discriminating input profiles. This boundary is intentionally device-independent: OpenAL device resampling and mixing are outside the owned simulation, while the exact source representation supplied to it is owned. A one-bit mutation control proves the comparator fails. | **DONE:** every represented event matches the actual Java source descriptor and the negative control fails. |

### 11. Performance, CPU/CUDA, and final closure

| ID | Gap and required run | Strict exit |
|---|---|---|
| `PERF-01` | **DONE:** machine-local frozen guards cover CPU scalar, full-feature batched CUDA, and 1080p CUDA render medians. The post-parity receipt passes at 4,141 steps/s, 2.83M env-ticks/s, and 65.87 fps against 3,858.9, 2.793M, and 24.2915 floors; the renderer also clears the separate 60 fps target. Disabled instrumentation remains registry-gated. | **DONE:** every owned regression median is above its frozen 95% floor, the trajectory hash is stable, and rebaselining remains an explicit maintainer-only operation. |
| `PERF-02` | **LIVE BOUNDED:** dense capacity fixtures cover living/XP/item/particle pools, horse atomic drops, chest/projectile backpressure, piston 63/64/65/257, scheduled ticks 4,095/4,096/4,097, dynamic growth, importer rejection, save/load, and the renderer. Saturation is atomic/growable rather than silently truncated. | **LIVE BOUNDED:** represented adversarial boundaries are machine-gated with no inactive hot-path allocation; broader cold-capacity stress remains strict-tail expansion. |
| `PERF-03` | **LIVE BOUNDED:** all shared state in a 2,048-lane mixed full-action run matches CPU bitwise on 64 sampled lanes over 75 decisions, and 64 CUDA lanes match CPU for every one of 2,058 chain ticks. CPU/CUDA raster is bit-exact for solid, cutout, translucent, D24 bias, mip, and fog cases. | **LIVE BOUNDED:** Linux CPU/CUDA state and raster parity pass. The pre-existing stale Metal helper manifest needs a Mac numeric rerun before re-recording; Java/native curriculum pickup tails are tracked separately and are not called backend parity. |
| `PERF-04` | **DONE:** the uninterrupted native runtime/save continuation battery repeatedly exercises checkpoints, dimensions, dense mechanics, UI/audio state, and reset/save paths; atomic save-slot and SDL save/quit/reload gates pass separately. The retained multi-client receipt adds 32 isolated affinity-pinned lanes and 9.12 cumulative client-hours: all outputs are byte-identical, all processes pass, summed peak RSS is 9.80 GiB, every lane stays below 1 GiB, and every lane reports zero major faults and zero swaps. The same revision passes the isolated CPU guard at 4,203 steps/s against the 3,858.9 frozen floor and 4,062 baseline. | **DONE:** repeated save/reset and dense mixed-feature continuation are stable under multi-client concurrency with explicit per-process memory, fault, swap, and deterministic-output evidence. |
| `PERF-05` | **LIVE BOUNDED:** registry, callback, ownership, gap, Java-oracle, native runtime, build, parity, performance, and quick-sweep lanes are fail-closed. Current tick-zero and curriculum artifacts are regenerated from this checkout; known Java/native and pixel residuals are named in `OPEN_DIVERGENCES.md` instead of hidden as passes. | **LIVE BOUNDED:** the 62-row closure queue is empty with evidence for every bounded owner. A pristine-box full GPU sweep, Mac Metal rerun, exhaustive strict residual closure, and human playthrough signoff remain release-tail work. |

## Registry status interpretation

Run:

```bash
uv run --no-project python verify/completeness/registry_gate.py
```

The manifest is deliberately conservative. A `live_bounded` entry can have a
large, high-quality implementation while still pointing to a general AI,
state, dimension, or persistence TODO. `render_only` never counts as gameplay.
`state_only` never counts as a complete entity lifecycle. Closing a TODO must
update both this file and the manifest classification in the same commit.

## First implementation tranche

The recommended next execution tranche is:

1. `HAR-02` through `HAR-07`: complete snapshot, comparison, fork, and reducer.
2. `SAVE-01`, `SAVE-03`, `SAVE-05`, `SAVE-06`, and `SAVE-07`: arbitrary valid
   save identity and complete ordering, still fail-closed.
3. `ENT-02` and `ENT-03`: horse and llama families, because these are the next
   clearest whole-feature gaps and exercise the generalized save contract.
4. `AI-05`, `WORLD-01`, and `RED-01`: broad mixed-order/topology campaigns that
   find false confidence in already implemented systems.
5. Then drain the remaining state queue before the pixel/audio closure queue.

This ordering gives every later feature the same reusable `S0` proof instead
of adding another one-off oracle fixture.
