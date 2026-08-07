# Open divergences

Active gaps only. Resolved work is recorded in git history and
`docs/DEVLOG.md`. Closed, retracted, or superseded entries keep their full
forensics in `CLOSED_DIVERGENCES.md` (a stub with the close date and one-line
resolution stays here in place); read there before re-investigating anything
that smells like a settled question.

Last verified on `d7f2998`, which includes the reviewed capture, preview, and
exact-hand-gate commits (`44a7de3`, `27ec2fc`, `d198a96`).

Pixel-perfect means every owned, A/B-stable pixel is equal. Mean-error budgets,
hard-pixel floors, empty target captures, and unstable Oracle pairs are not
passes.

## Generation parity

Simple dungeon rooms are live in the Overworld with exact represented block
generation, placement-stream chest loot seed, chest facing, all three
`chests/simple_dungeon` loot pools, and Forge-weighted skeleton/zombie/spider
spawner selection. The remaining G-01 gap is mineshafts: the existing kernel
is not yet admitted into live generation, and corridor/crossing/stair piece
metadata, cave-spider spawners, chest-minecart loot, rails, cobweb behavior,
entity persistence, and complete Java region placement still require one
end-to-end promotion. Dungeon spawner delay/save state and general spawner NBT
also remain part of the broader tile-entity persistence backlog.

## Brewing parity

Live five-slot brewing stands, exact fuel/timer and registry recipes, bottle
filling, drinkable potion effects, milk cure, break drops, comparator fullness,
state-capsule persistence, and the brewing GUI are implemented under
`--brewing on`. Thrown splash and lingering potions now use the Java throwable
heading/drag/gravity path, consume the held stack, render as the carried potion
item, emit the exact colored 2002/2007 event, scale player duration/instant
effects by impact distance, extinguish fire for water bottles, and create the
exact 10-tick-wait/600-tick shrinking lingering cloud with five-tick scans,
20-tick per-entity reapplication, quarter-duration effects, and radius-on-use.
Splash instant effects now cover represented living mobs with exact undead
health/damage reversal, distance rounding, hurt immunity, death/drop, and
healing caps. Lingering instant effects use independent EID/slot deadlines,
and water bottles damage every represented blaze/enderman in range. Cold
script fixtures resume an in-flight potion's age/pose/motion or a cloud's
complete represented scalar lifecycle and expose the next state for exact A/B
continuation. Represented living mobs now retain a bounded active-effect list
with vanilla amplifier/duration combination and pre-decrement cadence. Splash
and lingering potions deliver those effects with exact rounded/quarter
durations; regeneration, poison, undead applicability, same-tick fire
resistance, Speed/Slowness movement, Strength/Weakness melee, and Jump Boost
execute live. Resistance applies exact post-hurt-immunity reduction across the
represented mob damage routes, including periodic magic damage, and Wither
uses exact pre-decrement cadence plus the ordinary death/drop path. Health
Boost changes healing/regeneration caps and clamps health on removal;
Absorption grants, upgrades, consumes, expires, and exposes the live shield;
Levitation applies the exact post-move vertical transform and expiry. General
represented living mobs now expose reloadable air, use the exact type-specific
eye-in-water test, reset to 300 when dry, and drown every 320 submerged ticks;
Water Breathing holds the current counter, and a drown pulse consumes all 48
bubble-particle floats before represented damage/death. Invisibility now sets
the live entity render flag from active effect state: the normal survival
viewer omits the base model and slime gel layer, expiry restores them, and the
independent fire overlay remains visible. Player Night Vision now applies
`EntityRenderer.getNightVisionBrightness`'s exact duration/partial-tick
warning flicker to the post-provider lightmap and the post-fogColor1 clear,
terrain, water, and lava fog colors. The sealed-tunnel real-Java tape
`scenario_night_vision_dark_20260805T202020Z` is physics-clean over 50 ticks;
inventory, entity, and world-state gates pass, and the pixel gate passes its
three non-stale captured frames with no unexplained clusters. The warning
flicker is bit-exact in the Java/CPU numeric gate but remains pixel-open because
tapes do not yet record each framebuffer's `renderPartialTicks`. Player
Blindness now applies the exact last-19-tick fog-distance fade, the
post-fogColor1 void/Blindness darkening, and the sky/terrain linear fog ranges
with Blindness taking precedence over water and lava fog. The accepted
`scenario_blindness_dark_20260805T204050Z` real-Java tape is physics-clean over
50 ticks; inventory, entity, world-state, and structural pixel gates pass all
four non-stale frames. Ctrl and double-tap sprint starts are suppressed through
the final represented effect tick, expiry enables a same-tick start, and an
already active sprint continues. Blindness suppression of critical attacks
now runs through the represented player attack path. A locked real-Java
50-case fixture matches raw target-health, motion, and fire state for the falling/on-ground,
sprinting, riding, water, Blindness, and 0.9-cooldown boundaries; base damage,
Sharpness/Smite ordering, zombie armor, all 25 vanilla sword/axe/pick/shovel/hoe
attack speeds, one-versus-two durability wear, ordinary accepted-hit knockback,
grounded vertical clamping, sprint and Knockback-enchantment impulses, attacker
slowdown, and sprint cancellation are also exact.
Fire Aspect I/II preignition, accepted-hit extension, existing-fire maximum,
rejected-hit rollback/preservation, lethal ignition, and cooked pig loot are
exact as well.
Critical/enchantment sounds and
particles, sweeping, Thorns, player-target velocity
acknowledgement, statistics, and criteria remain open.
Remaining F-03 gaps are the other
mob world, render, and
non-brewing effect behaviors, direct-hit self-return edges, cloud
particles, automatic Java
capsule translation and full NBT persistence for thrown entities/clouds,
custom potion NBT, automation-side insertion/extraction, and promotion of the
remaining real-Java GUI/potion pixel tapes.

## Redstone/container parity

Closed single and reciprocal double trapped-chest comparator state is exact
for block ID 146 and the 27/54-slot inventory formulas. Live single
trapped-chest use/close is also exact: `numPlayersUsing` supplies clamped weak
power, strong power is upward-only, open/close notifies the chest and block
below, lid floats match, and both consumers retain their ordered +4 lamp-off
callbacks. The saved-state boundary still deliberately rejects open or
lid-transient chests; that broader capsule state and double-chest GUI
composition remain open.

Saved dispenser and dropper comparator sources are also exact for their
distinct block IDs 23/158, nine-slot inventory state, Java-float fullness
formula, pending callback, analog output, blocks, and light. Their pool is
cold and bounded. Live hoppers and droppers are active, and dispensers match
the focused Java fixtures for default stone, arrows, splash/lingering potions,
fire charges, fireworks, oak boats, water/lava buckets, TNT, eggs, snowballs,
experience bottles, and flint-and-steel air/failure/TNT paths. Multi-slot random
selection, sided/double inventories, remaining item behaviors/variants, and
broad save/load remain in the automation backlog.

Saved jukebox comparator state is exact for empty/record metadata agreement,
the bounded untagged vanilla-record set, and analog outputs 1 through 12.
Insertion and ejection now emit the exact Java 1010 item/zero event pairs for
all 12 records. A-01 supplies an ordered represented-event stream and optional
OpenAL playback for 88 events/199 owned OGG variants, including four bounded
streamed record voices. Firework blast/large/far and twinkle selection, exact
twinkle age, seeded pitch, and distance-delayed playback are promoted. World
event 2001 resolves all 235 registered non-air block IDs through Java's twelve
break-sound material families with exact volume and pitch bits. Music, ambient
loops, category controls, exact Java asset-variant cursor, remaining emitters,
and output comparison remain open.

Command-block comparator output is exact for IDs 137/210/211 inside a strict
inert saved subset: empty command, default name/output tracking and per-ID
auto mode, no last output/result stats, no powered/condition state, no pending
execution callback, and `successCount` in 0..15. Command execution, powered or
scheduled command lifecycle, output text, and result-stat persistence remain
open and are not implied by this comparator slice.

Saved item-frame comparator input is exact for the bounded empty/plain-stone
subset: one frame at the represented air cell behind one normal cube, facing
the comparator direction, emits zero when empty or `rotation + 1` for
rotation 0..7. Exact pose, hanging position, entity ID, facing, item tuple,
rotation, uniqueness, support, comparator tile, and pending work are all
gated. Frame damage, drops, maps/tags, rendering, and general lifecycle remain
open and are not implied by this static-input slice.

Observer ID 218 is exact for the bounded six-facing pulse lifecycle: watched
face filtering, +2 activation and +2 release callbacks, duplicate and powered
suppression, directional weak/strong output, placement pulses, same-time
observer chains, proof-safe pending-callback restore, and powered pending
removal notifications through one normal cube. Observer mutation and callback
paths inspect only adjacent cells; no loaded-world scan or idle tick path was
added.

The first piston subset is exact for a normal piston in all six facings,
powered by a non-output-side redstone block with air in front. Separate
EAST-facing fixtures prove the same immediate transition when an adjacent
floor lever, stone button, or wooden button flips from metadata 5 to powered
metadata 13. All four pressure-plate IDs also supply direct power for nonzero
strength, including an exact light-weighted strength-7 proof. A lit floor
torch on a non-attachment side now supplies its directional weak output too;
the complementary attachment-face fixture keeps the piston retracted and
matches Java's block-light 6 through the opacity-zero piston base. A powered
repeater likewise drives the piston only when its metadata-derived output
points at the queried side; the rotated powered-repeater control stays
retracted. A saved powered comparator requires the same directional match
plus a positive tile output; its rotated and zero-output controls remain
retracted. Powered-comparator emission is also exact at 9. A live observer
pulse supplies piston power only on its metadata-derived output face: the
SOUTH-watching positive extends the piston to its north, while the
EAST-watching rotated control pulses without extending it. The subset
also accepts powered dust only on its represented weak-output axis, and
relays dust strong power through one adjacent Java-normal cube. Perpendicular
dust controls prove that represented metadata power alone is insufficient
both directly and through the cube. Power on the piston output/front face is
excluded, while the exact five-cell `pos.up()` quasi-connectivity pass accepts
a one-up/one-side source and rejects its below-diagonal mirror. The subset
preserves the same-tick block-event boundary,
facing-specific base extension and moving-head metadata, moving block 36 for
two observations, exact 0/0.5/1 tile progress internally, and settlement to
head 34 on the third tile tick.
Straight pushable-stone lines are also exact. One- and two-stone fixtures hold
the moving head and each moved stone as independent block-36 tiles for two
observations, then settle head 34:5 and every stone 1:0 on the third. The
bounded far-to-near traversal accepts the vanilla maximum of 12 stones
(13 active entries including the head) and rejects a 13-stone line without
changing any member of the structure.
The first movable-block reaction slice is exact as well. Birch planks 5:2
prove that a registry-backed non-stone NORMAL state keeps ID and metadata
through block-36 movement and settlement. Obsidian is the exact BLOCK
no-move control. Dandelion 37:0 is exact both directly in front and after one
moved stone; red flower 38:2 independently proves `BlockFlower.damageDropped`
metadata preservation. A supported floor torch 50:5 separately proves that
block orientation is not copied into item damage: its exact drop is 50:0.
Supported redstone wire 55:0 proves a block-to-item-ID change: its exact drop
is registered redstone item 331:0.
Supported fire 51:0 supplies the zero-drop control. Its saved prestate
contains one exact future block-51 callback; Java retains that callback after
the moving block replaces the fire, but creates no local EntityItem and does
not consume World.rand for a drop. Snow layer 78:3 supplies the filtered-drop
control: Forge constructs five candidate snowball stacks and consumes five
World.rand chance draws, but piston chance -1 suppresses them all without
Math.random or entity-ID work. Brown and red mushrooms prove the shared
default-drop class while independently retaining registered item IDs 39 and
40 with damage zero. An east-facing attached ladder separately strips block
orientation metadata 5 to item damage zero. Its tick-zero input is a
redstone-block placement beside an already-staged piston, so it also proves
that indirectly queued piston block events drain immediately at the restored
input boundary. The Java bridge previously applied cursor restore, queued
that event, and deferred it until excluded world/client work had advanced
World.rand, Math.random, and next-entity-ID state; unconditional post-edit
block-event drain corrects the boundary while remaining a no-op for an empty
queue. Support-independent cobweb 30:0 adds another deterministic
block-to-item mapping: `BlockWeb.getItemDropped` returns string item 287:0.
Ordinary/lit pumpkins 86:3/91:3 share `BlockPumpkin`; both strip horizontal
facing to item damage zero, and replacing lit pumpkin with moving block 36
drains its emitted-light field exactly. Structure void 217:0 proves a second
no-item mechanism: its drop method is an empty override, so destruction
advances no drop RNG or entity cursor.
Each one-item DESTROY path creates the exact EntityItem,
including authoritative ID, RNG-derived pose/motion/yaw, age, pickup delay,
and item stack. Items tick before moving piston tiles, then use six-direction
swept head/full-cube collision with Java's overlap-plus-0.01 displacement and
per-axis 0.51 piston clamp. Native tests cover both drop positions and all six
movement directions. The fixed 64-entry piston set and 48-item pool have
one-branch empty paths, no heap allocation, and no loaded-world scan.

The control slice adds exact DESTROY payloads for lever, both buttons, four
pressure plates, lit/unlit redstone torches, and powered/unpowered repeaters,
plus block-specific break notifications for four powered indirect-lamp
fixtures. Its bounded EntityItem SELF path also resolves represented
normal-cube and half-extended piston-head collisions before piston swept
motion, matching Java tick order. Native regressions exhaust all 118 admitted
canonical control states. Current candidate evidence is the 69-case piston
family at
`c/magma/trace/out/matrix_redstone_piston_control_settled_head_family_1/summary.md`
and the exact-current-source 214-case aggregate at
`c/magma/trace/out/matrix_redstone_piston_control_settled_head_full_1/summary.md`.
All 214 state and raw-block gates pass; 211 behavior gates pass and three rows
are explicitly not required. The first exact-source rerun exposed one further
settled-piston-head collision: Java stopped a west-moving lever item at
x=14.125 against the ID-34 head plate at tick 3, while magma continued
through it. The active-item path now reuses captured
`BlockPistonExtension` plate and non-SHORT arm shapes for all six static-head
facings, including their one-cell-neighbor overhang. The failure and focused
fix are retained at
`c/magma/trace/out/redstone_piston_control_destroy_bundle_current_1/summary.md`
and
`c/magma/trace/out/redstone_piston_control_settled_head_fix_1/summary.md`.
Dead bush 32:0 adds the first randomized-count multi-item DESTROY path:
captured source consumes one `nextInt(3)`, then emits 0–2 separate stick
280:0 stacks with independent chance/offset, Math.random, and entity-ID
sequences. The count-two probe/fix is at
`c/magma/trace/out/redstone_piston_dead_bush_destroy_{probe,fix}_1/summary.md`.
The expanded piston family passes 70/70 at
`c/magma/trace/out/matrix_redstone_piston_dead_bush_family_1/summary.md`, and
the exact-current-source aggregate passes 215/215 at
`c/magma/trace/out/matrix_redstone_piston_dead_bush_full_1/summary.md`: all
215 state/raw-block gates and 212 required behavior gates pass, with three
explicitly not-required rows.
Sapling 6 adds the complete 12-state type/stage metadata rule: item damage
retains wood type 0..5 and strips stage bit 8. Oak stage 0 and dark-oak stage
1 deliberate/focused evidence is at
`c/magma/trace/out/redstone_piston_sapling_destroy_{probe,fix}_1/summary.md`.
The expanded piston family passes 72/72 at
`c/magma/trace/out/matrix_redstone_piston_sapling_family_1/summary.md`, and
the exact-current-source aggregate passes 217/217 at
`c/magma/trace/out/matrix_redstone_piston_sapling_full_1/summary.md`: all 217
state/raw-block gates and 214 required behavior gates pass, with three
explicitly not-required rows.
A successful extension has one further state-causal side effect even when
audio is disabled: `BlockPistonBase.eventReceived` consumes a World.rand
`nextFloat()` for pitch after movement and the extended-base write. A single
center redstone placement now drives opposed WEST/EAST pistons in vanilla
neighbor order, making the second exact sapling drop a same-boundary cursor
discriminator. Omitting the pitch draw diverges only the second EntityItem at
tick zero while raw blocks remain exact at
`c/magma/trace/out/redstone_piston_dual_sound_rng_probe_1/summary.md`; the
focused correction passes at
`c/magma/trace/out/redstone_piston_dual_sound_rng_fix_1/summary.md`. The
piston family passes 73/73 at
`c/magma/trace/out/matrix_redstone_piston_dual_sound_rng_family_1/summary.md`,
and the complete matrix passes 218/218 at
`c/magma/trace/out/matrix_redstone_piston_dual_sound_rng_full_1/summary.md`:
all 218 state/raw-block gates and 215 required behavior gates pass, with three
explicitly not-required rows.
Tall grass 31:0..2 adds the separate process-global `Block.RANDOM` cursor.
That state is causal for `BlockTallGrass.getDrops` but absent from vanilla
world NBT, so the oracle/capsule now captures and restores its private 48-bit
cursor explicitly. Controlled internal seed 0 takes the successful three-draw
path (`nextInt(8)`, weight-10 choice, `nextInt(1)`) and emits wheat seeds
295:0; seed 1396 takes the one-draw no-drop path. The deliberate omission of
only `nextInt(1)` keeps raw blocks and ordinary state features exact but fails
the specialized tick-zero cursor gate at
`c/magma/trace/out/redstone_piston_tall_grass_block_rng_probe_1/summary.md`.
Both focused branches pass at
`c/magma/trace/out/redstone_piston_tall_grass_fix_1/summary.md`; the piston
family passes 75/75 at
`c/magma/trace/out/matrix_redstone_piston_tall_grass_family_1/summary.md`, and
the complete matrix passes 220/220 at
`c/magma/trace/out/matrix_redstone_piston_tall_grass_full_1/summary.md`: all
220 state/raw-block gates and 217 required behavior gates pass, with three
explicitly not-required rows.

Wheat 59:0..7 is now exact for piston destruction. Ages 0..6 emit one seed
295:0 without mature-count RNG; age 7 emits wheat 296:0 followed by three
`World.rand.nextInt(14)` seed trials. Internal seed zero produces 0/4/9 and
therefore two separate seed stacks. The original controlled negative at
`c/magma/trace/out/redstone_piston_mature_wheat_probe_3/summary.md` proves the
missing extension/items/cursors. The implemented comparison exposed one
earlier block outcome: moving piston material is solid, so the crop's farmland
support changes from 60:0 to default dirt 3:0 through
`BlockFarmland.neighborChanged`. Canonical metadata and material solidity now
come from the live 1.11.2 registry export. Invalid wheat metadata and
insufficient entity capacity reject atomically. Both focused cases pass at
`c/magma/trace/out/redstone_piston_wheat_fix_3/summary.md` with exact item
order, EIDs, raw blocks, and RNG cursors.

The controlled-input state gate now records cursor bundles immediately before
and after every edit. It requires absolute equality from equal starts and
otherwise compares exact Java-LCG transition counts, entity-ID delta, and
32-bit updateLCG delta. This removes ambient loaded-chunk/client work between
later edits without discarding causal RNG checks. The deliberate comparator
negative is in `game/test_script.sh`; the 11 old absolute-cursor false
positives are retained at
`c/magma/trace/out/matrix_redstone_piston_wheat_full_1/summary.md`. The targeted
13-case correction passes at
`c/magma/trace/out/controlled_transition_fix_1/summary.md`, and the complete
current-source matrix passes 222/222 at
`c/magma/trace/out/matrix_redstone_piston_wheat_full_2/summary.md`: all 222
state/raw-block gates and 219 required behavior gates pass, with three
explicitly not-required rows.

Leaves 18/161 now cover all canonical type/decay metadata aliases, exact
sapling and oak/dark-oak apple chance paths, Java RNG order, atomic capacity
preflight, adjacent CHECK_DECAY marking, and the vanilla 9-cube/four-round
log-connectivity decay scan. The deliberate missing-behavior probes are
retained; the four piston cases, three natural-decay cases, and combined 7/7
focused set pass at `c/magma/trace/out/redstone_piston_leaves_fix_1/`,
`c/magma/trace/out/leaf_decay_fix_2/`, and
`c/magma/trace/out/leaves_focused_fix_1/`. The piston family passes 81/81 at
`c/magma/trace/out/matrix_redstone_piston_leaves_family_1/summary.md`, and the
complete current-source aggregate passes 229/229 at
`c/magma/trace/out/matrix_redstone_piston_leaves_full_1/summary.md`.

A valid two-block reed column now covers support-recursive teardown. Piston
DESTROY of lower 83:7 emits item 338:0, then its ordered neighbor notification
makes upper 83:11 fail `canBlockStay`, emit the second exact item, become air,
and continue notifying upward. The deliberate pre-fix failure and focused
correction are at `c/magma/trace/out/redstone_piston_reed_column_{probe,fix}_1/`.
The piston family passes 82/82 and the full matrix passes 230/230 at
`c/magma/trace/out/matrix_redstone_piston_reed_column_{family,full}_1/`.
Capacity for the entire column is checked before RNG, entity, piston, or world
mutation.

Cactus support teardown is now exact. The stable fixture starts cactus 81:9
on sand with stone below, then pushes stone into the west-adjacent cell. Block
36 remains non-triggering through both progress observations; when the moving
tile settles to real stone on tick three, its ordered neighbor notification
destroys the cactus after the ordinary entity pass and emits item 81:0 at age
0 with pickup delay 10. The clean pre-fix mismatch is retained at
`c/magma/trace/out/redstone_piston_cactus_settlement_probe_2/summary.md`, and
both the settlement plus an uncontaminated controlled-neighbor item proof pass
at `c/magma/trace/out/redstone_piston_cactus_fix_2/summary.md`. Native coverage
also proves that one free entity slot rejects a predicted two-cactus upward
cascade before partial RNG, piston, entity, or world mutation. The piston
family is 84/84 and the full exact-current-source matrix is 232/232 at
`c/magma/trace/out/matrix_redstone_piston_cactus_{family,full}_1/`.

Chorus support teardown is now exact. Chorus flower 200 accepts end stone or
chorus plant directly below, or exactly one horizontal plant with air on the
other three sides and below. Chorus plant 199 implements both direct and
side-supported predicates. Invalid blocks queue at +1; flowers disappear
without a drop, while plants consume `World.rand.nextInt(2)` and emit chorus
fruit 432:0 only on zero. Direct piston destruction and a moved-stone
settlement prove exact queue timing, item payload, RNG cursors, and atomic
capacity rejection. Focused evidence passes at
`c/magma/trace/out/chorus_support_fix_1/summary.md`,
`c/magma/trace/out/chorus_piston_payload_fix_1/summary.md`, and
`c/magma/trace/out/chorus_flower_settlement_fix_4/summary.md`. The expanded
four-random-seed full matrix passes 241/241 at
`c/magma/trace/out/matrix_redstone_piston_chorus_full_1/summary.md`: all 241
state/raw-block gates, 235 required behavior gates, and six not-required rows.

Double-plant 175 paired teardown is now exact for lower rose, both controlled
grass RNG branches, fern, and upper-half destruction. The lower half owns the
item payload, both cells are removed in Java notification order, and capacity
is preflighted before any partial mutation or RNG advance. The five focused
cases pass at
`c/magma/trace/out/double_plant_piston_fix_1/summary.md`; the intermediate
four-random-seed matrix passes 246/246 at
`c/magma/trace/out/matrix_redstone_piston_double_plant_full_1/summary.md`.

Bed 26 paired teardown is exact in both directions. Foot-first destruction
removes the head and emits bed item 355:0; head-first destruction delegates
the same single item to the foot callback and removes both cells. Controlled
World/Math/entity cursor transitions and full-pool rollback are exact at
`c/magma/trace/out/bed_piston_fix_1/summary.md`. The latest full matrix passes
248/248 at
`c/magma/trace/out/matrix_redstone_piston_bed_full_1/summary.md`: all state and
raw-block gates, 242 required behavior gates, and six not-required rows.

All seven door blocks, oak 64, iron 71, and wood variants 193..197, are now
exact for paired piston teardown. Native coverage exhausts eight canonical
lower metadata states and four canonical upper states per type. Lower halves
emit one type-specific registered item 324, 330, or 427..431; upper halves
emit no item directly and their ordered callback removes the lower half, which
owns the one drop. Lower-first and upper-first fixed-pool rejection is atomic.
The retained negative and focused exact evidence is at
`c/magma/trace/out/redstone_piston_door_{probe,fix}_1/`. The complete matrix
passes 256/256 at
`c/magma/trace/out/matrix_redstone_piston_door_full_1/summary.md`: all state and
raw-block gates, 250 required behavior gates, and six not-required rows.

Occupied flower-pot 140 teardown is exact. The capsule and Java authority
carry the tile's contained item and metadata, and the cold C pool allocates
only when a flower pot is present. Piston teardown follows Forge's drop order:
pot item 390:0, then the contained item, with exact entity IDs, World/Math RNG
cursors, tile retirement, block transitions, and fixed-pool atomicity. The
retained initial correction at
`c/magma/trace/out/redstone_piston_flower_pot_fix_1/summary.md` exposed the
missing contained drop; the corrected focused case passes at
`c/magma/trace/out/redstone_piston_flower_pot_fix_2/summary.md`. The current
matrix passes 255/255 in 202.281 seconds at
`c/magma/trace/out/matrix_redstone_piston_flower_pot_full_1/summary.md`: all
state and raw-block gates, 251 required behavior gates, and four not-required
rows.

Ownerless skull 144 teardown is exact across all six skull types and sixteen
rotation values. The cold tile pool and capsule retain type and rotation;
teardown emits ItemSkull 397 with the tile-owned type as metadata, then
retires the tile. Focused real-game evidence passes at
`c/magma/trace/out/redstone_piston_skull_fix_1/summary.md`. The current matrix
passes 256/256 in 206.681 seconds at
`c/magma/trace/out/matrix_redstone_piston_skull_full_1/summary.md`: all state
and raw-block gates, 252 required behavior gates, and four not-required rows.
Player-profile skull teardown is exact for a complete signed GameProfile. The
Java authority serializes the actual uncompressed owner compound; the capsule
validates every NBT tag with bounded depth/size, hashes a sidecar, and C retains
it in allocate-on-use cold state. The dropped ItemSkull 397:3 wraps that exact
compound under `SkullOwner`; typed comparison ignores compound insertion order
while preserving tag widths, float bits, list order, UUID, name, property
values, and signatures. Focused evidence passes at
`c/magma/trace/out/redstone_piston_player_skull_fix_2/summary.md`. The aggregate
passes 258/258 in 217.468 seconds at
`c/magma/trace/out/matrix_redstone_piston_player_skull_full_1/summary.md`: all
state/raw-block gates, 254 required behavior gates, and four not-required rows.

Closed shulker-box teardown is exact across all 16 colors and six facings. The
cold container and tagged-item pools preserve plain inventories, nested item
tags, custom names, locks, and deferred loot-table/seed state into the colored,
unstackable ItemBlock's complete `BlockEntityTag`; custom names are also
duplicated under `display.Name`. Deferred loot is not materialized. The direct
spawn path consumes three World.rand offset draws without the ordinary chance
draw. Focused plain-box real-game evidence passes at
`c/magma/trace/out/redstone_piston_shulker_box_fix_2/summary.md`. The current
rich/loot focused proof passes 2/2 at
`c/magma/trace/out/redstone_piston_shulker_nbt_probe_2/summary.md`. The hardened
32-client matrix passes 260/260 in 222.136 seconds at
`c/magma/trace/out/matrix_redstone_piston_shulker_nbt_full_2/summary.md`: all
state and raw-block gates, 256 required behavior gates, four not-required
rows, and no retries.

Representative piston BLOCK reactions are now exact for anvil mobility,
unbreakable end-portal frame, chest tile state, and an already-extended
piston, in addition to the existing obsidian control. Flowing/static
water/lava IDs 8..11 are exact zero-drop DESTROY payloads for all 64 metadata
states in native coverage, with source-water and source-lava independently
verified against real Java. The current 32-client matrix passes 266/266 with
no retries in 220.390 seconds at
`c/magma/trace/out/matrix_redstone_piston_fluid_block_full_1/summary.md`: all
state/raw-block gates, 262 required behavior gates, and four not-required
rows pass.

Cake 92 metadata 0..6 is exact zero-drop DESTROY state. It advances no drop
RNG or entity cursor and succeeds with a full entity pool. A three-bite cake
behind a comparator begins at analog output 8, is replaced by moving head
36:5, clears the comparator on its exact +2 callback, and turns its lamp off
at +4. Moving block 36 and settled piston head 34 are admitted as valid
zero-strength rear inputs rather than rejected as unknown analog sources.
Focused whole-cake, circuit, and saved-head evidence passes, and the complete
cake aggregate passes 269/269 in 237.138 seconds at
`c/magma/trace/out/matrix_redstone_piston_cake_full_2/summary.md`.

Melon block 103 is exact for all sixteen raw metadata values. Forge's
fortune-zero path consumes `World.rand.nextInt(5)` and the otherwise invisible
`nextInt(1)`, then creates three through seven separate item-360 stacks. Every
stack consumes the ordinary chance/offset draws, four `Math.random` doubles,
and one consecutive entity ID before the later piston-pitch draw. Controlled
internal World seeds zero and one prove the three- and seven-drop boundaries
at `c/magma/trace/out/redstone_piston_melon_fix_1/summary.md`. Native coverage
also proves six free slots reject a seven-drop result without partial block,
piston, RNG, or entity mutation. The complete 32-client matrix passes 271/271
with no retries in 214.462 seconds at
`c/magma/trace/out/matrix_redstone_piston_melon_full_1/summary.md`: all state
and raw-block gates, 267 required behavior gates, and four not-required rows.

Pumpkin/melon stems 104/105 are exact for ages 0..7. Their Forge override uses
the process-global `Block.RANDOM`, consuming three `nextInt(15)` trials and
emitting one crop-specific seed stack for each result at most the age. The
zero-drop branch advances no World/Math RNG or EID before piston pitch; each
successful trial emits a separate item 361:0 or 362:0 stack. The moving head
notifies supporting farmland 60:0 to become dirt 3:0. Controlled zero- and
three-drop Java proofs pass 3/3 at
`c/magma/trace/out/redstone_piston_stem_fix_1/summary.md`. Native coverage also
proves all ages, both item mappings, a one-drop branch, invalid metadata, full
pool zero-drop success, and atomic three-drop rejection with only two free
slots. The complete 32-client matrix passes 274/274 in 243.841 seconds at
`c/magma/trace/out/matrix_redstone_piston_stem_full_1/summary.md`: all state
and raw-block gates, 270 required behavior gates, and four not-required rows.
One unrelated button case passed after its isolated worker was recycled.

Vine 106 is exact for all sixteen attachment masks. An ordinary piston break
is not a shears harvest, so it emits no item and consumes no item capacity or
drop RNG. Waterlily 111 is exact for all sixteen raw metadata values and emits
one normalized item 111:0 over unchanged source water. The focused proof
passes 2/2 at
`c/magma/trace/out/redstone_piston_vine_waterlily_fix_1/summary.md`; native
coverage exhausts both metadata domains and proves full-pool vine success plus
atomic waterlily rejection.

Nether wart 115 is exact for ages 0..3. Ages 0..2 emit one item 372:0 without
a count draw; age 3 consumes `World.rand.nextInt(3)` and emits two through four
separate stacks before the ordinary per-stack RNG path. Focused age-0 and
controlled four-drop evidence passes 2/2 at
`c/magma/trace/out/redstone_piston_nether_wart_fix_1/summary.md`. Native
coverage proves every age, invalid metadata 4..15, both mature count
boundaries, and atomic rejection when only three slots remain for four drops.

Dragon egg 122 emits one normalized item 122:0. Cocoa 127 emits one brown dye
351:3 at ages 0/1 and three separate dyes at age 2. Their focused exact
results are `c/magma/trace/out/redstone_piston_dragon_egg_fix_1/summary.md` and
`c/magma/trace/out/redstone_piston_cocoa_fix_1/summary.md`. Native coverage
exhausts valid metadata and atomic capacity rejection.

The complete 32-client matrix passes 281/281 with no retries in 220.296
seconds at `c/magma/trace/out/matrix_redstone_piston_cocoa_full_2/summary.md`:
all state and raw-block gates, 277 required behavior gates, and four
not-required rows. The first aggregate exposed an uncontrolled scheduled
chorus RNG boundary. Applying the callback fixture after the tick-boundary
block mutation and using the parallel repeat lane passed 16/16 independent
processes before promotion.

The latest clean performance evidence is
`c/magma/trace/out/perf_guard_redstone_piston_cocoa_1.json`:
5,133 scalar steps/s, 2.93M Blaze env-ticks/s, and 31.84 1080p CUDA fps, all above the
unchanged frozen floors. It uses the exact current runtime and native sm_120
renderer; the dragon-egg/cocoa/vine/waterlily/wart/stem/cake/melon reaction paths add no idle
scan or allocation.
Three earlier control-slice failures remain preserved as environmental
evidence; no threshold or baseline was weakened.

Tripwire hook 131 and wire 132 are now exact for isolated and attached piston
teardown, attached-line notification order, represented entity activation,
hook weak/strong power, and the saved +10 hook/wire recheck lifecycle. The
capsule admits proof-bounded pending hook callbacks, and block-light treats
both thin blocks as zero-opacity. The deliberate old-C activation probe is at
`c/magma/c/magma/trace/out/redstone_tripwire_item_occupied_probe_1/summary.md`;
the corrected focused proof passes at
`c/magma/trace/out/redstone_tripwire_item_occupied_fix_2/summary.md`. A player
now crosses the non-solid wire, activates the complete line, leaves it, and
releases power at the exact +10 callback; the focused seven-case family passes
at `c/magma/trace/out/redstone_tripwire_family_release_fix_1/summary.md`. The
complete matrix passes 288/288 with no retries in 230.123 seconds at
`c/magma/trace/out/matrix_redstone_tripwire_release_full_2/summary.md`: all
state and raw-block gates, 284 required behavior gates, and four not-required
rows. The latest clean performance evidence is
`c/magma/trace/out/perf_guard_redstone_tripwire_release_1.json`: 4,991 scalar
steps/s, 2.93M Blaze env-ticks/s, and 30.54 1080p CUDA fps, all above unchanged
floors.

Boat activation now has a direct real-game proof. `EntityBoat` inherits
`doesEntityNotTriggerPressurePlate=false`, calls `doBlockCollisions`, and uses
the exact 1.375-by-0.5625 box. The 12-tick Java-vs-magma tripwire case matches
the boat state, all three simultaneous segment callbacks, both hooks and
lamps, seven raw mutations, and every one of 10,625 block/light cells at
`c/magma/trace/out/matrix_boat_redstone_6/summary.md`. The independent
two-tick negative matches an empty scheduled queue and 10,625 unchanged cells
when the boat overlaps a stone MOBS-sensitive plate at
`c/magma/trace/out/matrix_boat_redstone_negative_1/summary.md`. Wooden and
weighted EVERYTHING plates include the same boat. The implementation reuses
the existing fixed represented-entity pass with no world scan or allocation.
The final native aggregate passes at
`c/magma/trace/out/test_runtime_boat_redstone_final.log`. The GPU 1 performance
guard passes at `c/magma/trace/out/perf_guard_boat_redstone_1.json`: 5,073
scalar steps/s, 2.94M Blaze env-ticks/s, and 31.44 1080p CUDA fps. All three
remain above the unchanged machine-local floors.

XP-orb activation now has the same direct proof. `EntityXPOrb` inherits the
default trigger rule, uses a 0.5-by-0.5 box, and calls `doBlockCollisions`
after its ordinary move. The focused Java-vs-magma gate powers the middle
tripwire segment, both hooks, and both lamps on tick zero with five exact raw
mutations. The paired two-tick control leaves a stone MOBS-sensitive plate
unpowered with an empty queue and all 10,625 cells unchanged. Both entity
states and block-light volumes are exact at
`c/magma/trace/out/matrix_xp_redstone_2/summary.md`. The active path captures
post-move boxes during the existing fixed 64-orb pass; the inactive path adds
no orb-store scan or allocation. The full native aggregate passes at
`c/magma/trace/out/test_runtime_xp_redstone.log`, and the GPU 1 guard passes at
`c/magma/trace/out/perf_guard_xp_redstone.json`: 5,157 scalar steps/s, 2.93M
Blaze env-ticks/s, and 31.81 1080p CUDA fps.

Arrow activation now covers tripwire and pressure-plate sensitivity as well
as wooden buttons. Local 1.11.2 `EntityArrow` uses a 0.5-by-0.5 box, inherits
the default trigger rule, and calls `doBlockCollisions` after moving. The
12-tick focused case matches the powered middle segment, both hooks and lamps,
the wire-only release boundary, the +10 reschedule, five raw circuit
mutations, and all compared entity/state fields. The paired two-tick control
leaves a stone MOBS-sensitive plate unpowered, with an empty queue and 10,625
unchanged cells. Both state, behavior, and raw-block gates pass at
`c/magma/trace/out/matrix_arrow_redstone_2/summary.md`. A tick-zero trace-only
divergence exposed that generic projectile serialization had leaked
small-fireball acceleration fields onto arrows; the type-specific serializer
and its arrow/fireball negative controls now pass at
`c/magma/trace/out/test_script_arrow_schema.log`. The full native aggregate
passes at `c/magma/trace/out/test_runtime_arrow_redstone_3.log`, and the GPU 1
guard passes at `c/magma/trace/out/perf_guard_arrow_redstone.json`: 5,055
scalar steps/s, 2.94M Blaze env-ticks/s, and 31.44 1080p CUDA fps. The runtime
reuses the fixed 32-projectile pool and visits only an active arrow's crossed
cells; no loaded-world scan or heap allocation was added.

Falling-block activation now has the next represented-entity proof. Local
1.11.2 `EntityFallingBlock` uses a float-exact 0.98-by-0.98 box, inherits the
default pressure trigger predicate, and reaches `doBlockCollisions` through
`Entity.move` before drag and landing settlement. The deliberate old-C probe
at `c/magma/trace/out/redstone_tripwire_falling_sand_probe_1/summary.md`
fails first at tick zero because the capsule/runtime landing proof treated a
null-collision tripwire as a solid stop. The corrected two-case promotion at
`c/magma/trace/out/matrix_falling_redstone_1/summary.md` passes both the new
12-tick tripwire crossing and the existing 20-tick landing lifecycle with 25
matching state features, zero divergences, and exact raw blocks. Sand appears
on observation 1, powers the middle wire, both hooks, and both lamps on
observation 10, and matches all eleven entity trajectory rows, the exact
queue, source removal, and five circuit mutations. Native controls add wooden
and gold plate positives, a stone MOBS-sensitive negative, and a zero-motion
small-fireball non-trigger. The full native aggregate passes at
`c/magma/trace/out/test_runtime_falling_redstone.log`; its run used one CPU
core and approximately 85 MB RSS. The GPU 1 guard passes at
`c/magma/trace/out/perf_guard_falling_redstone.json`: 5,083 scalar steps/s,
2.94M Blaze env-ticks/s, and 31.32 1080p CUDA fps. Collision work remains
behind the existing nonzero falling-block branch, traverses only crossed
cells, and scans the fixed 16-entry store only for active occupancy.

Small-fireball tripwire occupancy now preserves vanilla's split between
activation and retention. `EntityFireball.onUpdate` never invokes block
collisions, so the two-tick unpowered-wire control remains inert. A powered
wire's scheduled `BlockTripWire.updateState` query still sees the fireball as
an ordinary pressure-triggering entity and must retain power and reschedule at
+10. The old-C probe fails on the sole wire metadata and pending-list boundary
at `c/magma/trace/out/matrix_small_fireball_tripwire_probe_1/summary.md`; the
two-case corrected gate passes state, behavior, light, and all 10,625 raw cells
at `c/magma/trace/out/matrix_small_fireball_tripwire_1/summary.md`. The native
aggregate passes at
`c/magma/trace/out/test_runtime_small_fireball_tripwire.log`. The GPU 1 guard
passes at `c/magma/trace/out/perf_guard_small_fireball_tripwire.json`: 5,147
scalar steps/s, 2.94M Blaze env-ticks/s, and 31.15 1080p CUDA fps. The
implementation reuses the bounded 32-projectile pool only in redstone
occupancy queries and adds no allocation or loaded-world scan.

Redstone dust now uses the complete Java 1.11.2 support predicate. Registry
capture schema `qrl.blockstate_props.v2` records `isFullyOpaque` for every one
of the 4,096 legacy ID/metadata inputs and pins the canonical JSON at SHA-256
`4d7a77822b3cb2591c94c2c81608520ff87dca172e80d2e5c8059c0309a4a2f0`.
`BlockRedstoneWire.canPlaceBlockAt` consumes that stateful mask plus its
explicit glowstone exception. The original glowstone old-C probe changes only
the source while Java powers dust and lamp at
`c/magma/trace/out/matrix_wire_glowstone_probe_1/summary.md`. Clean top-slab
and upside-down-stair probes reproduce the same first dust-metadata divergence
at `c/magma/trace/out/matrix_wire_fully_opaque_probe_2/summary.md`; the snow
probe becomes clean after retaining its own stone support at
`c/magma/trace/out/matrix_wire_full_snow_probe_1/summary.md`. All four corrected
circuits pass state, blocks, and light at
`c/magma/trace/out/matrix_wire_fully_opaque_1/summary.md`. The native aggregate
includes top/bottom slab and stair, eight/seven-layer snow, glowstone, and
glass controls and passes at
`c/magma/trace/out/test_runtime_wire_fully_opaque.log`. GPU 1 performance
passes at `c/magma/trace/out/perf_guard_wire_fully_opaque.json`: 5,134 scalar
steps/s, 2.93M Blaze env-ticks/s, and 31.20 1080p CUDA fps. Support lookup is
one generated mask read while gathering an already-active bounded component;
it adds no idle scan or allocation.

Dust component topology now matches Java's overhead predicate. Same-level dust
continues under a normal-cube ceiling. A one-block climb is allowed whenever
the block above the lower wire is not a Java normal cube, so glass headroom is
valid; stone headroom powers only the lower wire to 15 and leaves the upper
line and lamp off. The deliberate old-C run rejects all three covered
components at `c/magma/trace/out/matrix_wire_headroom_probe_1/summary.md`.
The corrected flat-ceiling, glass-climb, stone-blocked, and original air-climb
set passes exact state, blocks, and light 4/4 at
`c/magma/trace/out/matrix_wire_headroom_1/summary.md`. Native equivalents and
the complete runtime suite pass at
`c/magma/trace/out/test_runtime_wire_headroom.log`. GPU 1 performance passes
at `c/magma/trace/out/perf_guard_wire_headroom.json`: 5,058 scalar steps/s,
2.93M Blaze env-ticks/s, and 31.31 1080p CUDA fps. Removing the invalid
air-cover rejection adds no scan; the existing bounded component gather reads
one extra normal-cube mask only when considering an upward edge.

Dust directional weak output now uses Java 1.11.2's complete
`BlockRedstoneWire.isPowerSourceAt` shape predicate rather than a same-level ID
shortcut. This includes dust one block above a normal adjacent cube when the
current wire has headroom, dust one block below a non-normal adjacent cell,
axis-only repeater/comparator connections, facing-only observer connections,
and every vanilla block whose state can provide redstone power. The clean
old-C climb probe passes its connected-face control but changes the
perpendicular lamp from air to lit ID 124 where Java places unlit ID 123 at
`c/magma/trace/out/matrix_wire_directional_climb_probe_1/summary.md`. The
corrected climb/descent positive and negative controls and two diode-axis
controls pass 6/6 with exact state, queue, raw blocks, and light at
`c/magma/trace/out/matrix_wire_directional_output_candidate_1/summary.md`.
Eight affected earlier wire, headroom, branch, and piston cases also pass at
`c/magma/trace/out/matrix_wire_directional_output_regression_1/summary.md`.
Native climb/descent and repeater/comparator assertions plus the complete
runtime suite pass at
`c/magma/trace/out/test_runtime_wire_directional_output.log`. GPU 1 performance
passes at `c/magma/trace/out/perf_guard_wire_directional_output.json`: 5,098
scalar steps/s, 2.94M Blaze env-ticks/s, and 31.78 1080p CUDA fps. The exact
shape query is reached only by existing local redstone power queries and adds
no per-tick scan, allocation, or inactive-world work.

Dust support invalidation now follows Java's `neighborChanged` lifecycle.
When `canPlaceBlockAt` becomes false, Magma consumes the exact chance/position
draws from `World.rand`, creates one redstone item 331 with the matching
process-global entity and Math RNG cursors, changes the wire to air, updates
the surviving component, and notifies its consumers. The old-C support-removal
probe leaves ID 55 floating while its unrelated-neighbor negative control
passes at `c/magma/trace/out/matrix_wire_support_loss_probe_1/summary.md`.
Those cases plus a powered two-wire line that drains 14-to-0 and turns its lamp
off at the exact +4 boundary pass 3/3 at
`c/magma/trace/out/matrix_wire_support_loss_1/summary.md`. Native exact spawn,
full fixed-pool rejection, downstream drain, and delayed lamp assertions plus
the complete runtime suite pass at
`c/magma/trace/out/test_runtime_wire_support_loss.log`. GPU 1 performance
passes at `c/magma/trace/out/perf_guard_wire_support_loss.json`: 5,046 scalar
steps/s, 2.93M Blaze env-ticks/s, and 30.99 1080p CUDA fps. Support validation
runs only when an existing local neighbor notification reaches dust; there is
no idle scan or allocation.

Redstone-torch neighbor updates now validate the torch's stored floor or wall
attachment with Forge 1.11.2's directional `isSideSolid` and
`canPlaceTorchOnTop` rules. Invalid support consumes the exact four drop draws,
emits the lit redstone-torch item 76:0 from either block 75 or 76, replaces the
torch with air, and preserves the lit block's second-ring redstone
notifications. The deliberate old-C floor and wall probes leave ID 76
floating while an unrelated-neighbor negative passes at
`c/magma/trace/out/matrix_redstone_torch_support_loss_probe_1/summary.md`.
All three corrected cases pass with exact raw blocks, item/RNG/entity state,
queue state, and block light at
`c/magma/trace/out/matrix_redstone_torch_support_loss_candidate_1/summary.md`;
eight affected inverter, strong-power, and piston cases pass at
`c/magma/trace/out/matrix_redstone_torch_support_loss_regression_1/summary.md`.
Native tests additionally cover fences, glass, stained glass, walls, slab and
stair halves, snow layers, hopper tops, unlit-item normalization, and bounded
full-pool rejection. The complete runtime suite passes at
`c/magma/trace/out/test_runtime_redstone_torch_support_loss.log`. GPU 1
performance passes at
`c/magma/trace/out/perf_guard_redstone_torch_support_loss.json`: 5,089 scalar
steps/s, 2.93M Blaze env-ticks/s, and 31.40 1080p CUDA fps. Attachment checks
run only on an existing neighbor notification, with no idle scan or allocation.

Lever and stone/wood button neighbor updates now validate their exact stored
attachment with the same directional Forge side-solidity helper. Losing that
support removes the control, emits normalized item 69:0, 77:0, or 143:0 with
exact World/Math RNG and entity cursors, and preserves powered-support
notifications. An already-restored button callback remains queued after the
block disappears and drains as stale work on its original due tick. The
deliberate old-C cases leave each control floating while the unrelated-neighbor
lever negative passes at
`c/magma/trace/out/matrix_redstone_control_support_loss_probe_1/summary.md`.
All four corrected cases pass at
`c/magma/trace/out/matrix_redstone_control_support_loss_1/summary.md`; ten
affected lever/button release and power cases pass at
`c/magma/trace/out/matrix_redstone_control_support_loss_affected_1/summary.md`.
Native tests cover all eight lever metadata orientations, slab/stair/snow and
hopper support, glass rejection, exact drops, and atomic full-pool failure.
The complete runtime suite passes at
`c/magma/trace/out/test_runtime_redstone_control_support_loss.log`. GPU 1
performance passes at
`c/magma/trace/out/perf_guard_redstone_control_support_loss.json`: 4,851 scalar
steps/s, 2.93M Blaze env-ticks/s, and 31.16 1080p CUDA fps. Validation remains
notification-driven and adds no idle scan or allocation.

All four pressure-plate IDs now validate the block below on every neighbor
update using Java's stateful `isFullyOpaque || instanceof BlockFence`
predicate. Invalid support emits normalized item 70:0, 72:0, 147:0, or 148:0
with exact World/Math RNG and entity cursors, removes the plate, preserves
powered notifications, and leaves restored callbacks to drain as stale work.
The corrected support edits fail in old C for all four types while the
oak-fence negative passes at
`c/magma/trace/out/matrix_redstone_pressure_plate_support_loss_probe_2/summary.md`.
The corrected family passes 5/5 at
`c/magma/trace/out/matrix_redstone_pressure_plate_support_loss_candidate_2/summary.md`;
11 affected release, occupancy, sensitivity, and weighted-strength cases pass
at
`c/magma/trace/out/matrix_redstone_pressure_plate_support_loss_affected_1/summary.md`.
Native tests cover every fence ID, top/bottom slab and stair states, snow
layers, hopper, glass, all four stale callback IDs, exact item lifecycles, and
full-pool atomicity. The complete runtime suite passes at
`c/magma/trace/out/test_runtime_redstone_pressure_plate_support_loss.log`.
GPU 1 performance passes at
`c/magma/trace/out/perf_guard_redstone_pressure_plate_support_loss.json`:
4,951 scalar steps/s, 2.93M Blaze env-ticks/s, and 31.90 1080p CUDA fps. The
check is reached only by an existing neighbor notification.

Repeater and comparator neighbor updates now apply Java's
`pos.down().isFullyOpaque()` stay predicate. Losing support emits normalized
item 356:0 for either repeater block or item 404:0 for either comparator block,
retires comparator tile state, preserves already-saved diode callbacks as stale
work, and performs Java's directional output and second-ring notifications.
The deliberate old-C matrix leaves all five unsupported diode states floating
while a top-slab negative passes at
`c/magma/trace/out/matrix_redstone_diode_support_loss_probe_1/summary.md`.
The exact promotion passes 6/6 at
`c/magma/trace/out/matrix_redstone_diode_support_loss_candidate_5/summary.md`,
including powered repeater/comparator lamp release, exact drop RNG/entity
identity, raw blocks, and raw light. Saved timing, locking, analog/subtract,
and powered piston-destruction regressions remain exact. Native tests cover all
five block/meta states, top/bottom slabs, stair halves, snow layers, hopper,
glass and fence rejection, stale work, tile retirement, and full-pool atomicity
at `c/magma/trace/out/test_runtime_redstone_diode_support_loss.log`. GPU 1
performance passes at
`c/magma/trace/out/perf_guard_redstone_diode_support_loss.json`: 5,042 scalar
steps/s, 2.93M Blaze env-ticks/s, and 30.69 1080p CUDA fps. The stay check runs
only on an existing neighbor notification and adds no idle scan or allocation.

Tripwire-hook neighbor updates now validate the serialized horizontal facing
against Forge's directional side-solid predicate. Losing that wall emits item
131:0 with exact RNG/entity state before the old hook's `breakBlock` lifecycle
detaches any complete line. The powered fixture releases the opposite hook and
three wire states, queues the east then west lamp callbacks at +4, and retains
the removed hook's already-saved +10 callback as stale work. Old C leaves both
unsupported hooks floating while the unrelated alternate-support removal
passes at
`c/magma/trace/out/matrix_redstone_tripwire_hook_support_loss_probe_1/summary.md`.
The corrected cases and eleven affected entity/projectile/player/piston cases
pass 14/14 at
`c/magma/trace/out/matrix_redstone_tripwire_hook_support_loss_affected_1/summary.md`.
Native tests cover all four facings, exact powered detach order, item lifetime,
and full-pool atomicity at
`c/magma/trace/out/test_runtime_redstone_tripwire_hook_support_loss.log`. GPU 1
performance passes at
`c/magma/trace/out/perf_guard_redstone_tripwire_hook_support_loss.json`: 5,065
scalar steps/s, 2.93M Blaze env-ticks/s, and 31.36 1080p CUDA fps. The new work
is notification-driven and adds no loaded-world scan or allocation.

Direct replacement of attached string or a hook now invokes the removed
tripwire block's Java `breakBlock` lifecycle after the replacement state is
visible. String removal powers both hooks and lamps immediately, retains one
hook callback through +9, detaches at +10, and orders east then west lamp-off
callbacks for +14. Direct hook removal emits no item, detaches the other hook
and all three wire states immediately, and advances `World.rand` through the
two detach-sound pitch draws exactly. The settled fixture has an empty initial
queue, so the deliberate old-C failures begin at controlled tick zero at
`c/magma/trace/out/matrix_redstone_tripwire_live_break_probe_2/summary.md`.
The corrected cases pass at
`c/magma/trace/out/matrix_redstone_tripwire_live_break_candidate_2/summary.md`,
and the 16-case affected family passes at
`c/magma/trace/out/matrix_redstone_tripwire_live_break_affected_1/summary.md`.
Native coverage passes at
`c/magma/trace/out/test_runtime_redstone_tripwire_live_break.log`. GPU 1
performance passes at
`c/magma/trace/out/perf_guard_redstone_tripwire_live_break.json`: 5,095 scalar
steps/s, 2.93M Blaze env-ticks/s, and 31.86 1080p CUDA fps. Both callbacks run
only when an existing represented block is replaced and add no idle work.

New tripwire string now invokes Java's `BlockTripWire.onBlockAdded` hook scan
after the placed state is visible. In the complete east-west proof, the scan
checks SOUTH then WEST, finds the west hook, validates the opposite-facing
east hook, changes both hooks and all three strings to attached state in the
placement tick, and schedules only the west hook for +10. That callback drains
without changing the completed line. Isolated placement is an exact negative
with no queue, RNG, entity, or extra block mutation. The deliberate old-C
result fails only the completed-line case at
`c/magma/trace/out/matrix_redstone_tripwire_on_add_probe_1/summary.md`; both
corrected cases pass at
`c/magma/trace/out/matrix_redstone_tripwire_on_add_candidate_2/summary.md`.
The 18-case affected tripwire family passes at
`c/magma/trace/out/matrix_redstone_tripwire_on_add_affected_1/summary.md`.
Native coverage passes at
`c/magma/trace/out/test_runtime_redstone_tripwire_on_add.log`. GPU 1 performance
passes at `c/magma/trace/out/perf_guard_redstone_tripwire_on_add.json`: 5,121
scalar steps/s, 2.94M Blaze env-ticks/s, and 32.04 1080p CUDA fps. The bounded
hook scan runs only on new string placement and adds no idle work or allocation.

Direct replacement of a powered pressure plate now runs Java's removed-block
lifecycle after the plate becomes air. `BlockBasePressurePlate.breakBlock`
notifies both the plate position and its support position for every nonzero
binary or weighted strength, so a lamp adjacent only to the support receives
the exact +4 release callback. Stone 70:1, wood 72:1, gold 147:2, and iron
148:1 all fail old magma at tick zero; unpowered stone 70:0 is the passing
negative at
`c/magma/trace/out/matrix_redstone_pressure_plate_direct_break_probe_1/summary.md`.
All five corrected cases pass at
`c/magma/trace/out/matrix_redstone_pressure_plate_direct_break_candidate_2/summary.md`,
and all 21 affected sensitivity, analog, saved-work, support, and direct-break
cases pass at
`c/magma/trace/out/matrix_redstone_pressure_plate_direct_break_affected_1/summary.md`.
Native coverage passes at
`c/magma/trace/out/test_runtime_redstone_pressure_plate_direct_break.log`.
GPU 1 performance passes at
`c/magma/trace/out/perf_guard_redstone_pressure_plate_direct_break.json`:
5,152 scalar steps/s, 2.93M Blaze env-ticks/s, and 31.42 1080p CUDA fps. The
callback runs only when a represented powered plate is replaced and adds no
idle work, allocation, item spawn, or RNG consumption.

Direct replacement of redstone wire now runs Java's removed-block lifecycle
after the dust becomes air. `BlockRedstoneWire.breakBlock` asks each of the six
adjacent positions to notify its own neighbors before the ordinary replacement
notification. This reaches a lit lamp adjacent only to the former dust support
and queues its exact +4 release. Powered wire 55:15 fails old magma at tick
zero while unpowered wire 55:0 is the passing no-callback negative at
`c/magma/trace/out/matrix_redstone_wire_direct_break_probe_1/summary.md`.
Both corrected cases pass at
`c/magma/trace/out/matrix_redstone_wire_direct_break_candidate_2/summary.md`,
and all 35 affected wire topology, support, strong-power, and piston cases pass
at
`c/magma/trace/out/matrix_redstone_wire_direct_break_affected_1/summary.md`.
The native aggregate passes at
`c/magma/trace/out/test_runtime_redstone_wire_direct_break_2.log`; its first
run exposed a leftover negative-fixture lamp beside a later torch support, and
clearing that completed fixture removed the false extra callback without any
product change. GPU 1 performance passes at
`c/magma/trace/out/perf_guard_redstone_wire_direct_break.json`: 5,156 scalar
steps/s, 2.93M Blaze env-ticks/s, and 31.94 1080p CUDA fps. The bounded
six-center traversal runs only on wire replacement and adds no idle work,
allocation, item spawn, or RNG consumption.

Direct replacement of repeaters and comparators now runs the old diode's
directional output teardown after the replacement is visible. Comparator
tiles retire before notification, matching `BlockRedstoneComparator.breakBlock`.
A powered SOUTH-facing repeater 94:0 or comparator 149:8 strongly powers a
normal output stone; the lamp adjacent only to that stone receives the exact
+4 release when the diode disappears. Unpowered 93:0 and 149:0 are strict
no-callback controls. The powered rows fail old magma while both controls pass
at `c/magma/trace/out/matrix_redstone_diode_direct_break_probe_4/summary.md`.
All four fixed rows pass at
`c/magma/trace/out/matrix_redstone_diode_direct_break_candidate_1/summary.md`,
and 14 direct, scheduled, saved, support-loss, and piston lifecycle cases pass
at
`c/magma/trace/out/matrix_redstone_diode_direct_break_affected_1/summary.md`.
The native aggregate passes in 4:12 with a 286 MB peak at
`c/magma/trace/out/test_runtime_redstone_diode_direct_break.log`. GPU 1
performance passes at
`c/magma/trace/out/perf_guard_redstone_diode_direct_break.json`: 5,142 scalar
steps/s, 2.93M Blaze env-ticks/s, and 30.83 1080p CUDA fps. The new branches
run only on direct diode replacement and add no idle work, allocation, item
spawn, or RNG consumption.

Direct placement of a repeater now runs the shared diode `onBlockAdded`
output-neighborhood notification after the powered or unpowered state becomes
visible. A SOUTH-facing powered 94:0 strongly powers its north stone and
immediately lights a lamp adjacent only to that stone. Unpowered 93:0 is the
strict no-callback control. Old magma misses only the powered lamp mutation at
`c/magma/trace/out/matrix_redstone_repeater_direct_add_probe_1/summary.md`.
Both corrected cases pass at
`c/magma/trace/out/matrix_redstone_repeater_direct_add_candidate_1/summary.md`,
and ten direct add/remove, three-direction power, delayed edge, and saved-work
cases pass at
`c/magma/trace/out/matrix_redstone_repeater_direct_add_affected_1/summary.md`.
The native aggregate passes in 3:55 with a 286 MB peak at
`c/magma/trace/out/test_runtime_redstone_repeater_direct_add.log`. GPU 1
performance passes at
`c/magma/trace/out/perf_guard_redstone_repeater_direct_add.json`: 5,145 scalar
steps/s, 2.93M Blaze env-ticks/s, and 30.66 1080p CUDA fps. The callback runs
only on repeater placement and adds no idle work or allocation.

Direct placement of a comparator now runs its inherited directional-output
notification before the output-zero comparator tile is created, matching
Java's `BlockRedstoneComparator.onBlockAdded` order. Aligned 149:1 placement
notifies a stale powered downstream repeater, which releases at +2 and hands
its lamp a +6 release. Rotated 149:3 is the strict empty-queue control. The
aligned row fails old magma while the rotated control passes at
`c/magma/trace/out/matrix_redstone_comparator_direct_add_probe_2/summary.md`.
Both fixed rows pass at
`c/magma/trace/out/matrix_redstone_comparator_direct_add_candidate_1/summary.md`,
and 14 direct, scheduled, saved, support-loss, and piston comparator cases
pass at
`c/magma/trace/out/matrix_redstone_comparator_direct_add_affected_1/summary.md`.
The native aggregate passes in 3:57 with a 285 MB peak at
`c/magma/trace/out/test_runtime_redstone_comparator_direct_add.log`. GPU 1
performance passes at
`c/magma/trace/out/perf_guard_redstone_comparator_direct_add.json`: 5,030
scalar steps/s, 2.93M Blaze env-ticks/s, and 31.66 1080p CUDA fps. Comparator
capacity is preflighted before mutation, and the new work is confined to
direct placement with no idle scan, allocation, item spawn, or RNG draw.

Direct placement of redstone wire now runs the complete vanilla
`BlockRedstoneWire.onBlockAdded` traversal after recomputing the connected
component: UP/DOWN neighbor centers, horizontal wire neighborhoods, and the
one-block vertical wire checks around horizontal neighbors. A zero-power wire
above a support reaches a diagonal stale powered repeater through the DOWN
center and queues its exact +2 release; an unpowered repeater is the strict
queue-free control. With only the two new call sites disabled, the powered row
fails while the control passes at
`c/magma/trace/out/matrix_redstone_wire_direct_add_probe_5/summary.md`. Both
fixed rows pass at
`c/magma/trace/out/matrix_redstone_wire_direct_add_candidate_2/summary.md`, and
14 flat, vertical, branch, support, removal, repeater, and comparator cases
pass at
`c/magma/trace/out/matrix_redstone_wire_direct_add_affected_1/summary.md`. The
native aggregate passes in 4:09 with a 287 MB peak and zero swap at
`c/magma/trace/out/test_runtime_redstone_wire_direct_add.log`. GPU 1
performance passes at
`c/magma/trace/out/perf_guard_redstone_wire_direct_add.json`: 5,108 scalar
steps/s, 2.93M Blaze env-ticks/s, and 31.90 1080p CUDA fps. The traversal is
fixed and placement-only, with no idle scan, allocation, item spawn, or RNG
draw.

Direct lamp placement now follows `BlockRedstoneLight.onBlockAdded` rather
than reusing the delayed neighbor-change lifecycle. Requested lit block 124
without power immediately normalizes to unlit 123 and creates no scheduled
callback. Requested unlit 123 with direct redstone-block power becomes 124;
without power it remains 123. Old magma fails only the lit-unpowered state at
`c/magma/trace/out/matrix_redstone_lamp_direct_add_probe_1/summary.md`. All
three corrected states pass at
`c/magma/trace/out/matrix_redstone_lamp_direct_add_candidate_1/summary.md`, and
17 lamp, lever, dust-topology, and indirect-power cases pass at
`c/magma/trace/out/matrix_redstone_lamp_direct_add_affected_1/summary.md`. The
native aggregate passes in 4:10 with a 287 MB peak and zero swap at
`c/magma/trace/out/test_runtime_redstone_lamp_direct_add.log`. GPU 1
performance passes at
`c/magma/trace/out/perf_guard_redstone_lamp_direct_add.json`: 5,114 scalar
steps/s, 2.93M Blaze env-ticks/s, and 31.90 1080p CUDA fps. The branch runs
only on direct lamp block-type changes and adds no idle scan, allocation,
item spawn, or RNG draw.

Direct lit-torch placement now runs the six-center
`BlockRedstoneTorch.onBlockAdded` traversal before the outer placement
notification. Its support-power check admits represented strong power through
a normal support, so the new torch queues its own +2 update as Java does.
The lit old-C row fails and the unlit queue-free control passes at
`c/magma/trace/out/matrix_redstone_torch_direct_add_probe_2/summary.md`. Both
corrected rows pass at
`c/magma/trace/out/matrix_redstone_torch_direct_add_candidate_1/summary.md`,
and all 16 placement, support-loss, strong-power, saved-callback, wall, and
burnout cases pass at
`c/magma/trace/out/matrix_redstone_torch_direct_add_affected_1/summary.md`.
The native aggregate passes in 3:55 with a 288 MB peak and zero swap at
`c/magma/trace/out/test_runtime_redstone_torch_direct_add.log`. GPU 1
performance passes at
`c/magma/trace/out/perf_guard_redstone_torch_direct_add.json`: 5,165 scalar
steps/s, 2.93M Blaze env-ticks/s, and 31.47 1080p CUDA fps. The added work is
placement- and callback-driven, with no idle scan or allocation.

Saved floor-torch callbacks now admit a normal support when every captured
power provider around it belongs to the represented redstone set. The
repeater proof also admits ordinary solid side neighbors, which Java ignores
for repeater locking. A restored lit torch above an indirectly powered support
turns off at +2; valid stale lit and indirectly powered unlit callbacks drain
without mutation. Old magma fails the two lit rows while the unlit control
passes at
`c/magma/trace/out/matrix_redstone_torch_saved_indirect_probe_1/summary.md`.
All three corrected rows pass at
`c/magma/trace/out/matrix_redstone_torch_saved_indirect_candidate_2/summary.md`,
and all 19 affected torch rows pass at
`c/magma/trace/out/matrix_redstone_torch_saved_indirect_affected_1/summary.md`.
The native aggregate passes in 4:10 with a 289 MB peak and zero swap at
`c/magma/trace/out/test_runtime_redstone_torch_saved_indirect.log`. GPU 1
performance passes at
`c/magma/trace/out/perf_guard_redstone_torch_saved_indirect.json`: 5,069
scalar steps/s, 2.93M Blaze env-ticks/s, and 31.45 1080p CUDA fps. Callback
admission is restore-only validation; dispatch remains queue-driven and adds
no idle scan or allocation.

Saved wall-torch callbacks now cover metadata 1/2/3/4 in both inverter
directions. The capsule resolves the attached support from each stored facing,
validates the normal/redstone-block support and all represented power
providers, and retains the existing bounded notification proof. With wall
admission disabled, the isolated EAST off/on pair fails at
`c/magma/trace/out/matrix_redstone_torch_wall_saved_probe_2/summary.md`. All
eight directional callbacks pass at
`c/magma/trace/out/matrix_redstone_torch_wall_saved_candidate_2/summary.md`,
and all 27 affected torch rows pass at
`c/magma/trace/out/matrix_redstone_torch_wall_saved_affected_1/summary.md`.
The native aggregate passes in 4:03 with a 291 MB peak and zero swap at
`c/magma/trace/out/test_runtime_redstone_torch_wall_saved.log`. GPU 1
performance passes at
`c/magma/trace/out/perf_guard_redstone_torch_wall_saved.json`: 5,109 scalar
steps/s, 2.93M Blaze env-ticks/s, and 30.37 1080p CUDA fps. The change only
widens cold capsule admission; per-tick runtime work is unchanged.

Saved torch callbacks now retain the full promoted Forge 1.11.2 directional
support surface instead of requiring a normal cube or redstone block. The
capsule and runtime share source-equivalent side-solid decisions for slab
halves, stair facing and actual inner/outer shape, full snow, farmland sides,
hopper UP, double slabs, and compressed redstone. Four floor fixtures cover a
top slab, upside-down stair, eight-layer snow, and hopper; two wall fixtures
cover farmland and the solid face of a stair. With the new admission disabled,
all six are omitted and fail at
`c/magma/trace/out/matrix_redstone_torch_saved_directional_support_probe_1/summary.md`.
The corrected focused set passes 6/6 at
`c/magma/trace/out/matrix_redstone_torch_saved_directional_support_candidate_3/summary.md`,
and all 33 affected torch rows pass at
`c/magma/trace/out/matrix_redstone_torch_saved_directional_support_affected_1/summary.md`.
Capsule and native negative controls reject a bottom slab, bottom stair,
partial snow, hopper side, and wrong stair face. The hopper also exposed the
old omitted-block opacity fallback: Java registers `BlockHopper` light opacity
zero, so a torch emitting 7 produces light 6 in the hopper cell. The dedicated
light suite locks that value and passes. The optimized native aggregate passes
in 4:03 with a 291 MB peak and zero swap at
`c/magma/trace/out/test_runtime_redstone_torch_saved_directional_support_optimized.log`.
GPU 1 performance passes at
`c/magma/trace/out/perf_guard_redstone_torch_saved_directional_support.json`:
5,095 scalar steps/s, 2.93M Blaze env-ticks/s, and 29.29 1080p CUDA fps. The
feature widens cold callback validation and corrects block-property lookup;
it adds no per-tick scan or allocation.

The remaining `Block.canPlaceTorchOnTop` exceptions are now exact at the saved
callback boundary: oak and nether fences, all five 1.8 wood-fence registry
variants, glass, stained glass, and cobblestone wall. Disabling only the
exception list omits every callback and makes all ten cases fail at
`c/magma/trace/out/matrix_redstone_torch_saved_top_exceptions_probe_1/summary.md`.
The first admitted run exposed a separate block-property error: IDs 188..192
and 95 transitioned exactly but their support cells remained light 0 instead
of Java's 6. Stained glass is now a promoted table row with hardness 0.3 and
opacity zero; all five newer wood fences now share `BlockFence` opacity zero.
The 169-ID property oracle passes Java == CPU == CUDA, and the consolidated
runtime sweep passes 10/10 with exact state, raw blocks, and block light at
`c/magma/trace/out/matrix_redstone_torch_saved_top_exceptions_candidate_3/summary.md`.
Capsule/native tests exhaust all ten tops and reject a fence used as a wall
support. The native aggregate passes with a 291 MB peak and zero swap at
`c/magma/trace/out/test_runtime_redstone_torch_saved_top_exceptions.log`.
GPU 1 performance passes at
`c/magma/trace/out/perf_guard_redstone_torch_saved_top_exceptions.json`: 5,018
scalar steps/s, 2.93M Blaze env-ticks/s, and 27.35 1080p CUDA fps. Admission is
cold-load-only and the corrected property reads use the existing table path.

Shears now disarm attached tripwire before the removed-block callback, matching
`BlockTripWire.onBlockHarvested` ordering. The middle string becomes air, both
hooks and the two remaining strings detach, and only the west hook's +10
recheck remains queued; neither lamp receives the ordinary powered alarm
pulse. The exact harvest also damages shears from metadata 0 to 1, charges
0.005 exhaustion, and emits one string EntityItem with exact World RNG,
Math.random, and entity-ID cursors. The deliberate callback-without-DISARMED
control fails at
`c/magma/trace/out/matrix_redstone_tripwire_shears_probe_1/summary.md`. The
corrected case and the shared stone-mining regression pass 2/2 with 25 matching
state features and all 10,625 raw block and light cells exact at
`c/magma/trace/out/matrix_redstone_tripwire_shears_affected_1/summary.md`.
Selection tests lock the attached 1/16-to-5/32 height and detached half-block
box. The native aggregate passes in 5:04 with a 284 MiB peak and zero swap at
`c/magma/trace/out/test_runtime_redstone_tripwire_shears.log`. GPU 1
performance passes at
`c/magma/trace/out/perf_guard_redstone_tripwire_shears.json`: 4,364 scalar
steps/s, 2.92M Blaze env-ticks/s, and 26.83 1080p CUDA fps. The feature adds no
idle scan or allocation.

The remaining tripwire-specific work is unrepresented entity families,
non-full moving collision shapes, non-item entity pushing, retraction and
sticky pistons, slime structure branching/motion, moving-block save/reload,
and rendering.

Carrots 141 and potatoes 142 now extend the registry-order DESTROY boundary.
All eight ages of each crop are covered natively. Mature crops consume three
exact `World.rand.nextInt(14)` trials; carrots append carrot 391 stacks,
potatoes append potato 392 stacks, and mature potato then consumes the
independent process-global `Block.RANDOM.nextInt(50)` poisonous-potato 394
trial. The deliberate old-C rejection is preserved at
`c/magma/c/magma/trace/out/redstone_piston_carrot_potato_probe_1/summary.md`.
The focused Java proof passes 3/3 at
`c/magma/trace/out/redstone_piston_carrot_potato_fix_1/summary.md`, and the
shared wheat/carrot/potato family passes 5/5 at
`c/magma/trace/out/redstone_piston_crop_family_fix_1/summary.md`.

The complete matrix passes 291/291 with no retries in 257.080 seconds at
`c/magma/trace/out/matrix_redstone_piston_carrot_potato_full_1/summary.md`:
291 state and raw-block gates, 287 required behavior gates, and four
not-required rows. The latest frozen performance guard passes at
`c/magma/trace/out/perf_guard_redstone_piston_carrot_potato_1.json`: 4,513
scalar steps/s, 2.93M Blaze env-ticks/s, and 25.34 1080p CUDA fps. Remaining
registry DESTROY work continues after potatoes 142.

Comparator blocks 149/150 now extend that registry-order DESTROY boundary.
Both IDs and all 16 metadata states emit comparator item 404:0 and retire the
saved comparator tile. Powered output teardown notifies the exact output
neighborhood, including the lamp's +4 release. A transient saved block 150 can
enqueue its +2 self-correction from piston placement before destruction, and
the stale callback remains ordered until WorldServer discards it at due time.
The deliberate old-C proof fails 3/3 at
`c/magma/trace/out/redstone_piston_comparator_destroy_probe_2/summary.md`; the
focused correction passes 3/3 at
`c/magma/trace/out/redstone_piston_comparator_destroy_fix_4/summary.md`, and the
five-case comparator/piston family passes at
`c/magma/trace/out/redstone_piston_comparator_family_fix_1/summary.md`.

The clean aggregate passes 294/294 in 267.206 seconds at
`c/magma/trace/out/matrix_redstone_piston_comparator_destroy_full_2/summary.md`:
294 state and raw-block gates, 290 required behavior gates, and four
not-required rows. An earlier aggregate's sole random-walk `on_ground` capture
flake passed 3/3 on fresh isolated clients before the clean rerun. GPU metrics
pass at 2.93M Blaze env-ticks/s and 28.15 CUDA fps; the contention-isolated
scalar confirmation passes at 4,122 steps/s. Remaining registry DESTROY work
continues after comparator 150.

Beetroot 207 closes the only missing generated-registry DESTROY entry after
comparator 150. Ages 0..2 emit one beetroot-seed stack 435:0 without mature
trials. Age 3 emits beetroot 434:0, then consumes three
`World.rand.nextInt(6)` trials and appends one seed for each draw at most 3.
The deliberate old-C rejection fails 2/2 at
`c/magma/trace/out/redstone_piston_beetroot_probe_1/summary.md`; the focused
fix passes 2/2 at
`c/magma/trace/out/redstone_piston_beetroot_fix_1/summary.md`, and the complete
wheat/carrot/potato/beetroot family passes 7/7 at
`c/magma/trace/out/redstone_piston_crop_family_beetroot_fix_1/summary.md`.
Native coverage exhausts all four canonical ages, rejects metadata 4, checks
exact item/RNG/EID order, and proves atomic insufficient-capacity rejection.

The aggregate passes 296/296 in 345.746 seconds at
`c/magma/trace/out/matrix_redstone_piston_beetroot_full_1/summary.md`: all 296
state and raw-block gates, 292 required behavior gates, and four not-required
rows. One unrelated pumpkin-stem oracle job timed out and passed after its
client was recycled. Current GPU guards pass at 2.93M Blaze env-ticks/s and
28.59 CUDA fps. Two scalar affinity samples under host load above 200 measured
3,735 and 3,844 steps/s against the 3,858.9 floor and remain recorded as
non-passes; the preceding quiet-host scalar confirmation is 4,122 steps/s.
Beetroot work is event-only and adds no idle scan or allocation. The generated
registry inventory after 207 contains only structure void 217 and shulker
boxes 219..234, which were already covered, so the registry-order DESTROY
audit is complete through 234.

Settled ordinary-piston retraction is now exact for the first EAST-facing
empty-front lifecycle. Removing the source replaces extended base 33:13 with
a retracting source moving block 36:5, removes head 34:5, retains progress
0.5/1.0 over the first two tile ticks, and settles base 33:5 on the third.
The contraction sound consumes exactly one `World.rand.nextFloat()`, advancing
the controlled cursor from 0 to 11 without EID, Math.random, Block.RANDOM, or
scheduled-work changes. The deliberate old-C proof fails at
`c/magma/trace/out/redstone_piston_empty_retraction_probe_1/summary.md`; the
three start/progress/settled cases pass at
`c/magma/trace/out/redstone_piston_empty_retraction_fix_1/summary.md`, and the
combined extension/retraction lifecycle passes 6/6 at
`c/magma/trace/out/redstone_piston_extension_retraction_family_fix_1/summary.md`.

The 299-case aggregate at
`c/magma/trace/out/matrix_redstone_piston_empty_retraction_full_1/summary.md`
passes 298 rows. Its only failure is the known `random_seed_1` landing capture:
`on_ground` diverged at tick 16 and induced a 0.05 exhaustion difference.
Three fresh isolated clients pass 3/3 at
`c/magma/trace/out/random_seed_1_retraction_isolated_remeasure_1/summary.md`.
Current-source GPU guards pass at 2.94M Blaze env-ticks/s and 24.84 CUDA fps.
Scalar samples under elevated host load measured 3,725 and 3,665 steps/s
against the unchanged 3,858.9 floor and remain non-passes. Retraction starts
only on neighbor edits and reuses the existing bounded piston tile tick; it
adds no idle scan or allocation.

The first sticky lifecycle is exact for an EAST-facing piston and one stone.
Extension distinguishes the typed sticky head state 36:13 from the moved
stone state 36:5, advances both moving tiles through progress 0.5/1.0, and
settles head 34:13 plus stone at the destination. Retraction creates the
source tile first and the pulled-stone tile second, removes the old head and
stone origin, then settles base 29:5 with stone in the former head cell. The
controlled contraction sound advances World.rand from seed 0 to 11 while the
entity, Math.random, Block.RANDOM, and scheduled-work cursors remain unchanged.
The deliberate old-C pull proof fails at
`c/magma/trace/out/redstone_sticky_piston_one_stone_pull_probe_1/summary.md`.
All six start/progress/settled extension and pull cases pass at
`c/magma/trace/out/redstone_sticky_piston_one_stone_fix_1/summary.md`; the
normal/sticky lifecycle family passes 12/12 at
`c/magma/trace/out/redstone_piston_normal_sticky_lifecycle_family_1/summary.md`.

The clean aggregate passes 305/305 with no retries in 268.544 seconds at
`c/magma/trace/out/matrix_redstone_sticky_piston_one_stone_full_1/summary.md`:
305 exact state and raw-block gates, 301 required behavior gates, and four
not-required rows. GPU guards pass at 2.93M Blaze env-ticks/s and 28.08 CUDA
fps. A scalar sample under elevated host load measured 3,650 steps/s against
the unchanged 3,858.9 floor and remains a non-pass; the preceding clean sample
is 4,122 steps/s. Sticky work is event-driven and reuses the fixed moving-tile
set, adding no idle scan or allocation. Other sticky facings and targets,
empty/immovable sticky retraction, mid-extension cancellation, slime
branching, non-item pushing, and moving-tile save/reload remain open.

One-observation minimum pulses now reverse in-flight normal and sticky
one-stone extensions. At source removal Java clears the extending head tile
immediately. A normal piston leaves the moved-stone tile active to finish at
the destination; a sticky piston additionally clears that tile and uses the
result as the exact flag that suppresses pulling it back. Both then create the
retracting source tile and consume the contraction sound draw. The old C guard
ignored the neighbor change and remained extended. Its deliberate failure is
`c/magma/trace/out/redstone_sticky_piston_minimum_pulse_probe_1/summary.md`.
The first corrected sticky proof passes at
`c/magma/trace/out/redstone_sticky_piston_minimum_pulse_fix_1/summary.md`, the
normal/sticky pair passes 2/2 at
`c/magma/trace/out/redstone_piston_minimum_pulse_normal_sticky_fix_1/summary.md`,
and the affected lifecycle plus observer family passes 15/15 at
`c/magma/trace/out/redstone_piston_minimum_pulse_lifecycle_family_1/summary.md`.
Native coverage checks both intermediate tile sets, two exact sound draws,
final blocks, and the observer-created pulse.

Two complementary full aggregates cover all 307 current cases. The first
passes 306/307 in 269.863 seconds at
`c/magma/trace/out/matrix_redstone_piston_minimum_pulse_full_1/summary.md`; its
only scheduled-fire capture miss passes 3/3 at
`c/magma/trace/out/fire_spread_scheduled_minimum_pulse_isolated_remeasure_1/summary.md`.
The second passes that row and 305 others in 299.716 seconds at
`c/magma/trace/out/matrix_redstone_piston_minimum_pulse_full_2/summary.md`; its
only random-walk landing miss passes 3/3 at
`c/magma/trace/out/random_seed_1_minimum_pulse_isolated_remeasure_1/summary.md`,
and its recycled-client infrastructure retry passes. GPU guards pass at 2.93M
Blaze env-ticks/s and 28.56 CUDA fps against unchanged floors. Scalar timing
is deferred because an unrelated 48-core workload keeps host load above 60;
the last clean result remains 4,122 steps/s. The new branch is entered only
when an active extending source loses power and adds no idle scan or
allocation. Later removal/repower boundaries, other facings, broader sticky
targets, broader slime topologies, non-item pushing, and moving-tile
save/reload remain open.

The first bounded slime-structure branch is now exact. For an EAST-facing
normal piston, slime at the front attaches an UP stone, builds the same Java
move list, consumes it in reverse, and settles both blocks one cell east. For
an extended sticky piston, removing power builds the structure westward after
the old head is cleared, creates the retracting base first, then the attached
stone and slime moving tiles in exact reverse list order, and settles both in
the former head column. The deliberate old-C extension and pull probes fail
only on the attached structure cells. Corrected extension and pull start and
settled cases pass 4/4; the affected normal/sticky/minimum-pulse family passes
18/18 at
`c/magma/trace/out/redstone_piston_slime_structure_family_1/summary.md`.
Native controls prove the shared 12-block limit, base-only retraction when an
oversized sticky structure cannot move, and immovable side-obsidian exclusion.
The full aggregate passes 311/311 in 285.943 seconds at
`c/magma/trace/out/matrix_redstone_piston_slime_structure_full_1/summary.md`;
one unrelated water-case infrastructure timeout passed on its automatic
fresh-client retry. GPU guards pass at 2.93M Blaze env-ticks/s and 27.48 CUDA
fps against unchanged floors. The generic structure path uses fixed 12-move
and 48-destroy arrays and is entered only for an active slime piston event;
the ordinary straight-line path is unchanged. Broader slime topologies,
terminal DESTROY classes beyond simple deterministic payloads, other piston
facings, non-item pushing, and moving-tile save/reload remain open.

Simple terminal DESTROY cells on those slime structures are now exact. The
deliberate old-C extension and sticky-pull cobweb probes fail 2/2 because C
previously rejected a structure containing any destroy cell. The corrected
start and settled cases pass 4/4 at
`c/magma/trace/out/redstone_piston_slime_cobweb_destroy_promoted_1/summary.md`.
They prove Java's reverse destroy-before-reverse-move order, the cobweb-to-
string item mapping, exact World/Math RNG and entity-ID transitions, and an
empty scheduled queue. Native tests additionally cover two terminal destroys
on distinct slime branches and atomic failure when only one item slot is free.
The affected family passes 23/23 at
`c/magma/trace/out/redstone_piston_slime_destroy_family_1/summary.md`. The full
matrix passes 313/315 in 264.611 seconds at
`c/magma/trace/out/matrix_redstone_piston_slime_destroy_full_1/summary.md`; its
unrelated random-walk landing and trapped-chest timing captures each pass 3/3
on fresh isolated clients at
`c/magma/trace/out/slime_destroy_unrelated_isolated_remeasure_1/summary.md`.
The composite therefore covers all 315 current cases. GPU guards pass at
2.94M Blaze env-ticks/s and 25.12 CUDA fps. The event-only path retains fixed
12-move and 48-destroy storage. At this promotion boundary, paired,
cascading, stateful, and randomized terminal payloads remained explicitly
rejected.

Canonical bed pairs are the first paired/cascading exception. Four foot-
targeted extension/pull cases deliberately failed while four head-targeted
cases exposed an existing capacity-unaware success. The corrected eight-case
start/settled set passes 8/8 at
`c/magma/trace/out/redstone_piston_slime_bed_pair_fix_1/summary.md`. A foot
drops item 355 directly and its head later disappears without a second item;
a head drops nothing directly and its notified foot owns the one deferred
item. Preflight now reserves either path before mutation and native full-pool
coverage proves the deferred head path rejects atomically. The affected
family, including both ordinary-bed controls, passes 33/33 at
`c/magma/trace/out/redstone_piston_slime_bed_pair_family_1/summary.md`. The
full matrix passes a clean 323/323 with no retries in 294.370 seconds at
`c/magma/trace/out/matrix_redstone_piston_slime_bed_pair_full_1/summary.md`:
319 required behavior gates plus four not-required rows. GPU guards pass at
2.93M Blaze env-ticks/s and 25.06 CUDA fps.

Canonical door pairs are now the second paired/cascading exception. The
deliberate old-C probe fails all four lower-target extension/pull cases at
admission while the four capacity-available upper-target cases expose an
under-reserved accidental success. The corrected eight-case start/settled set
passes 8/8 at
`c/magma/trace/out/redstone_piston_slime_door_pair_fix_1/summary.md`. A lower
half supplies the direct door item; an upper half supplies no direct item and
its notified lower half owns the deferred item. Native coverage exhaustively
checks all seven door block/item mappings and all 84 canonical lower/upper
states, plus a full-pool upper negative that proves atomic rejection. The
affected family, including eight ordinary-door controls, passes 49/49 at
`c/magma/trace/out/redstone_piston_slime_door_pair_family_1/summary.md`. The
full matrix passes a clean 331/331 with no retries in 295.020 seconds at
`c/magma/trace/out/matrix_redstone_piston_slime_door_pair_full_1/summary.md`:
327 required behavior gates plus four not-required rows. GPU guards pass at
2.93M Blaze env-ticks/s and 24.48 CUDA fps. Non-bed/door paired or cascading
payloads, columns, stateful tiles, and randomized terminal payloads remain
open.

Deterministic double-plant pairs are now the third paired/cascading exception.
The deliberate old-C probe fails four lower-target double-rose cases while
four upper-target cases again expose capacity-unaware accidental success. The
corrected start/settled set passes 8/8 at
`c/magma/trace/out/redstone_piston_slime_double_rose_pair_fix_1/summary.md`.
Native coverage checks all 40 canonical lower/upper combinations for
sunflowers, lilacs, double ferns, double roses, and peonies. It also proves
fixed-pool upper rejection is atomic and keeps randomized double grass
explicitly rejected in this multi-entry path. The affected family, including
all five ordinary double-plant controls, passes 62/62 at
`c/magma/trace/out/redstone_piston_slime_double_rose_pair_family_1/summary.md`.
The full matrix passes 338/339 in 285.933 seconds at
`c/magma/trace/out/matrix_redstone_piston_slime_double_rose_pair_full_1/summary.md`;
the unrelated random-walk capture miss passes 3/3 on a fresh isolated client
at `c/magma/trace/out/slime_double_rose_unrelated_random_remeasure_1/summary.md`,
so the composite covers all 339 cases: 335 required behavior gates plus four
not-required rows. GPU guards pass at 2.93M Blaze env-ticks/s and 27.98 CUDA
fps. At that promotion boundary, randomized double grass, other paired or
cascading payloads, columns, stateful tiles, and randomized terminal payloads
remained open.

Randomized double grass is now included in the paired exception. Ten required
cases cover lower and upper targets for normal extension and sticky pull plus
a mixed two-terminal extension whose NORTH lower rolls during reverse
destruction and whose SOUTH lower rolls later during notification. The
deliberate old-C probe fails 10/10; the corrected set passes 10/10 at
`c/magma/trace/out/redstone_piston_slime_double_grass_pair_fix_1/summary.md`.
Preflight advances a local 48-bit World.rand cursor through direct payloads
and deferred pair callbacks without mutating runtime state. Native tests prove
that a no-drop roll extends even with a full item pool, a selected drop rejects
that pool atomically, and two selected mixed-phase drops reject when only one
slot is free. The affected family passes 72/72 at
`c/magma/trace/out/redstone_piston_slime_double_grass_pair_family_1/summary.md`.
The full matrix passes 348/349 in 301.386 seconds at
`c/magma/trace/out/matrix_redstone_piston_slime_double_grass_pair_full_1/summary.md`;
the unrelated random-walk miss passes 3/3 at
`c/magma/trace/out/slime_double_grass_unrelated_random_remeasure_1/summary.md`,
so the composite covers all 349 cases: 345 required behavior gates plus four
not-required rows. GPU guards pass at 2.93M Blaze env-ticks/s and 27.48 CUDA
fps. A structure mixing a deferred random-grass callback with a destroyed
tripwire/hook remains explicitly rejected because that break hook consumes
sound RNG between the two modeled phases. Other paired/cascading payloads,
stateful tiles, randomized-count terminal payloads, and column families beyond
the represented reed/cactus paths remain open.
The remaining piston work, automation, broader item-frame lifecycle, and
broader command-block behavior remain ordered on
`c/magma/PARITY_PROJECT.md`.

Direct reed columns and valid cactus settlement columns are now represented
for lower/middle cells on normal slime extension and sticky slime pull. Reed
destruction uses the reverse direct-payload phase followed by bottom-up
notification drops and a bounded local RNG/capacity shadow. Cactus fixtures
begin diagonal to the side stone, because a cactus touching the terminal
stone before movement is already invalid Java state; the column collapses
only when block 36 settles to stone. The valid old-C probe fails all eight
reed cases, while the corrected 16-case behavior/raw gate passes after the
final combined-capacity fix at
`c/magma/trace/out/redstone_piston_slime_reed_cactus_column_final_1/summary.md`.
The 88-case family and 365-case full matrix pass every behavior and raw-block
gate at
`c/magma/trace/out/redstone_piston_slime_reed_cactus_column_family_1/summary.md`
and
`c/magma/trace/out/matrix_redstone_piston_slime_reed_cactus_column_final_1/summary.md`.
Four cactus settlement trajectories remain state diagnostics: their drops
occur two server ticks after the controlled input, after the integrated Java
world resumes unrelated loaded-chunk RNG and entity-ID activity. The strict
contract on those rows is the exact controlled start, raw column mutation,
item 81:0 count, age zero, and pickup delay ten. Native seeded-runtime tests
cover exact trajectories and atomic fixed-pool rejection. Final-source GPU
guards pass at 2.93M Blaze env-ticks/s and 25.8 CUDA fps. Other cascade families remain
open.

Sticky retraction now covers empty pull origins in all six facings, EAST
obsidian, headless serialized sticky and normal bases, and an EAST missing-head
sticky base with unrelated stone in front plus obsidian at the pull origin.
The first DOWN fixture was rejected because its alleged empty target landed in
the flat-world stone layer; after moving it upward, Java and old C agree. The
corrected old-C probe isolates only the two headless sticky rows failing at
`c/magma/trace/out/redstone_sticky_piston_retraction_boundary_probe_2/summary.md`.
Java accepts the queued event even when the head is absent. Magma no longer
requires a matching head, and clears the front only when vanilla does: normal
retraction, an admitted sticky pull, or matching-head removal after the base
becomes block 36. The strict 20-case correction passes at
`c/magma/trace/out/redstone_sticky_piston_retraction_boundary_fix_2/summary.md`;
the 56-case affected family passes all behavior/raw outcomes with 54 strict
state rows plus two existing delayed-cactus diagnostics at
`c/magma/trace/out/redstone_sticky_piston_retraction_boundary_family_1/summary.md`.
The 385-case aggregate passes 384 rows at
`c/magma/trace/out/matrix_redstone_sticky_piston_retraction_boundary_full_1/summary.md`.
Its unrelated scheduled-fire oracle miss passes 3/3 on fresh clients at
`c/magma/trace/out/fire_spread_scheduled_retraction_isolated_remeasure_1/summary.md`,
so the composite covers all 385 behavior/raw outcomes: 381 required gates plus
four not-required rows, 381 strict state passes, and four delayed-cactus
diagnostics. GPU guards pass at 2.93M Blaze env-ticks/s and 27.7 CUDA fps.
Other sticky movable/immovable target facings and later event boundaries remain
open.

Movable one-stone sticky extension and pull now has strict start/settled proof
in all six facings. The existing runtime was already direction-generic; the
new native loop locks exact moving base/head/stone metadata, ownership,
settlement, and sound RNG without adding production work. The focused 28-case
family passes at
`c/magma/trace/out/redstone_sticky_piston_directional_stone_family_1/summary.md`.
The expanded aggregate passes all 405 behavior/raw outcomes in 356.999 seconds
at
`c/magma/trace/out/matrix_redstone_sticky_piston_directional_stone_full_1/summary.md`,
with 401 strict state rows plus four delayed-cactus diagnostics. GPU guards
pass at 2.93M Blaze env-ticks/s and 29.31 CUDA fps.

Sticky pull reaction coverage now crosses the remaining five facings with
birch planks, structure void, an empty chest, an extended normal piston, and
an unextended normal piston. The existing bounded rules already match all ten
start/settled Java cases. A later repower boundary did expose a real bug:
power restored while the retracting base was block 36 left magma unextended,
while Java's settled `setBlockState` ran piston `onBlockAdded` and re-extended.
The two-cell failure is retained at
`c/magma/trace/out/redstone_sticky_piston_repower_during_retraction_probe_2/summary.md`.
Restored piston bases now receive one bounded post-retirement power check, so
new motion begins after the retiring tile loop and cannot advance early. The
focused fix and 65-case affected family pass at
`c/magma/trace/out/redstone_sticky_piston_repower_during_retraction_fix_1/summary.md`
and
`c/magma/trace/out/redstone_sticky_piston_target_reaction_repower_family_1/summary.md`.
The aggregate passes all 416 behavior/raw outcomes in 409.606 seconds at
`c/magma/trace/out/matrix_redstone_sticky_piston_repower_full_1/summary.md`,
with 412 strict state rows plus four delayed-cactus diagnostics. GPU guards
pass at 2.94M Blaze env-ticks/s and 27.38 CUDA fps.

Moving-piston tile persistence is now exact for the bounded active set. The
Java oracle and magma state rows compare moved block/meta, facing,
source/extending flags, and exact current/last progress bits. A checkpoint
taken with both a moving head and moving stone at progress 0.5 resumes to the
exact Java progress-1.0 state and final settled block volume at
`c/magma/trace/out/redstone_moving_piston_checkpoint_verify_final_1/summary.md`.
The expanded state comparator exposed four existing repower/remove boundaries
where magma created a progress-zero replacement tile one observation early;
the bounded powered-base recheck now runs at the next server-tick phase. The
final aggregate passes all 421 behavior/raw outcomes in 367.658 seconds at
`c/magma/trace/out/matrix_redstone_moving_piston_checkpoint_full_2/summary.md`,
with 417 strict state rows plus four delayed-cactus diagnostics. GPU guards
pass at 2.93M Blaze env-ticks/s and 25.02 CUDA fps. Vanilla BlockEvents are
transient live queues rather than disk-save state. Additional event
boundaries, broader slime topologies, and rendering remain open.

Moving-piston collision now includes the first represented living-mob slice.
The deliberate EAST NoAI-pig probe starts from an exact common state and
differs only in the entity x coordinate: Java pushes to
`12.959999988079071`, while the old magma result remains `12.5`, at
`c/magma/trace/out/redstone_piston_pig_push_probe_1/summary.md`. The bounded
moving-head sweep reuses the exact piston collision shapes, float mob sizes,
overlap-plus-0.01 displacement, and per-axis 0.51 clamp. Fresh EAST and UP
Java comparisons pass exact entity coordinates, moving tiles, blocks, light,
and all other observed state. Native coverage locks all six directions and an
off-axis negative. The 424-case aggregate passes in 362.343 seconds at
`c/magma/trace/out/matrix_redstone_piston_living_push_full_1/summary.md`, with
420 strict state rows and the same four delayed-cactus diagnostics. Performance
guards pass at 4,948 scalar steps/s, 2.93M Blaze env-ticks/s, and 28.94 CUDA
fps. The new scan is fixed-array and entered only while a moving tile and
represented living mob are both active. Player pushing, ordinary-physics mobs,
additional collision shapes, and rendering remain open.

Living-mob piston displacement now clips exactly against static full cubes.
In the deliberate wall probe, Java stops the pig at
`12.550000011920929`, while old magma incorrectly reaches
`12.959999988079071`; blocks and light remain exact at
`c/magma/trace/out/redstone_piston_pig_wall_push_probe_1/summary.md`. The fixed
wall case plus the existing open EAST and UP cases pass 3/3 against fresh Java
at `c/magma/trace/out/redstone_piston_pig_wall_push_fix_1/summary.md`. Native
coverage locks the same stationary second observation. The 425-case aggregate
passes in 383.029 seconds at
`c/magma/trace/out/matrix_redstone_piston_pig_wall_push_full_1/summary.md`, with
421 strict state rows and four delayed-cactus diagnostics. Performance guards
pass at 4,397 pinned scalar steps/s, 2.93M Blaze env-ticks/s, and 28.66 CUDA
fps. The initial unpinned scalar artifact records 3,684 steps/s immediately
after the Java pool stopped under high host load. The clipping scan is bounded
to the swept mob AABB and is entered only for an active moving tile and living
mob. Slabs, stairs, fences, walls, and other non-full collision
shapes remain open.

Soul sand is the first exact non-full static shape in piston living-mob
clipping. A DOWN source head above a NoAI pig distinguishes the 7/8 top face:
Java moves from y=80 to `79.875`, while old magma's full-cube approximation
leaves y=80 at
`c/magma/trace/out/redstone_piston_pig_soul_sand_vertical_probe_2/summary.md`.
The fixed vertical case, horizontal soul-sand side, full-cube wall, and open
EAST/UP controls pass 5/5 against fresh Java at
`c/magma/trace/out/redstone_piston_pig_soul_sand_fix_1/summary.md`; the full
native runtime suite also passes. The 427-case aggregate ran in 337.767
seconds at
`c/magma/trace/out/matrix_redstone_piston_pig_soul_sand_full_1/summary.md`.
Its sole aggregate-only failure was an unrelated broad random-walk Java
`on_ground` bit at tick 13; immediate isolated remeasurement passes all 25
supported features at
`c/magma/trace/out/random_seed_1_soul_sand_promotion_retry_1/summary.md`.
The composite therefore has 423 strict state passes plus the four existing
delayed-cactus diagnostics, and all 427 raw block gates pass in the aggregate.
Performance guards pass at 5,046 scalar steps/s, 2.94M Blaze env-ticks/s, and
31.27 CUDA fps. The new shape branch is inside the existing swept-cell scan;
the idle and no-piston paths remain unchanged.

All four 1.11.2 single-slab registry classes now participate in exact
piston-pushed living collision: stone slab 44, wooden slab 126, stone slab2
182, and purpur slab 205, with metadata bit 8 selecting the top half. The
deliberate bottom-stone-slab probe isolates Java y
`79.59000002384185 -> 79.5` against old magma
`79.49 -> 78.97999999999999` at
`c/magma/trace/out/redstone_piston_pig_bottom_slab_probe_1/summary.md`. The
first shape fix exposed a shared pig-height error: local 1.11.2 EntityPig
source specifies 0.9 x 0.9, while C had grouped pigs with 1.4-high cows.
After correcting the shared dimension, the open DOWN case and all four
registry/half-shape cases pass fresh Java at
`c/magma/trace/out/redstone_piston_down_pig_push_fix_1/summary.md` and
`c/magma/trace/out/redstone_piston_pig_slab_registry_fix_1/summary.md`.
Native coverage locks all four slab IDs and the six open piston facings; the
complete runtime suite passes. The clean 432-case aggregate passes in 336.622
seconds at
`c/magma/trace/out/matrix_redstone_piston_pig_slab_full_1/summary.md`, with
428 strict state rows, four delayed-cactus diagnostics, and 432 exact raw block
gates. Performance guards pass at 4,956 scalar steps/s, 2.94M Blaze
env-ticks/s, and 31.03 CUDA fps.

All fourteen 1.11.2 stair registry IDs now use the exact source collision
algorithm for piston-pushed living mobs: four horizontal facings, top/bottom
halves, and neighbor-derived straight, inner-left/right, and outer-left/right
shapes. The deliberate straight-stair probe isolates both sides of an
EAST-ascending oak stair. Java settles the west lane from
`79.59000002384185` to y=79.5 and holds the east lane at y=80, while old magma
lets both fall to `79.09000002384185`, at
`c/magma/trace/out/redstone_piston_pig_straight_stair_probe_1/summary.md`.
Fresh Java comparisons pass the two straight lanes, both discriminating outer
corners, an inner empty corner, and a top stair at
`c/magma/trace/out/redstone_piston_pig_straight_stair_fix_1/summary.md`,
`c/magma/trace/out/redstone_piston_pig_outer_stair_fix_1/summary.md`, and
`c/magma/trace/out/redstone_piston_pig_inner_top_stair_fix_1/summary.md`.
Native coverage locks every stair ID, facing, half, lane, and the connected
fixtures; the complete runtime suite passes. The 438-case aggregate passes in
394.894 seconds at
`c/magma/trace/out/matrix_redstone_piston_pig_stair_full_1/summary.md`, with
434 strict state rows, four delayed-cactus diagnostics, and 438 exact raw block
gates. Performance guards pass at 4,666 scalar steps/s on an idle physical
core, 2.93M Blaze env-ticks/s, and 24.69 CUDA fps. At that boundary, fences,
walls, and other non-full collision shapes remained open.

Fences, cobblestone walls, and fence gates now use their exact connected
1.11.2 collision shapes for piston-pushed living mobs. This covers all seven
fence registry IDs, material-separated wood/nether-fence connections, the two
wall metadata variants, all six gate registry IDs, open gates with no
collision, closed gate axes, four connection directions, and the wall's
special 3/8-wide straight runs. In the deliberate pre-fix probe, Java holds
the isolated fence post, its north arm, the isolated wall post, and its north
arm at y=80.5 while old magma moves each pig to `79.99 ->
79.47999999999999`; raw blocks and light remain exact at
`c/magma/trace/out/redstone_piston_pig_fence_wall_probe_1/summary.md`. The
straight-wall side gap is a measured negative with identical Java/C motion.
All five pass after the fix at
`c/magma/trace/out/redstone_piston_pig_fence_wall_fix_1/summary.md`. A separate
closed-gate probe fails old magma while the open-gate negative passes at
`c/magma/trace/out/redstone_piston_pig_fence_gate_probe_1/summary.md`; both
pass after the fix at
`c/magma/trace/out/redstone_piston_pig_fence_gate_fix_1/summary.md`. Native
coverage locks every registry ID, direction, gate state/axis, fence L-shaped
empty corner, and both straight-wall orientations; the complete runtime suite
passes. The connection predicate also matches Java's opaque-material/full-cube
test: redstone blocks and observers connect despite not being normal cubes,
while gourds do not. Those three discriminators pass together at
`c/magma/trace/out/redstone_piston_pig_fence_connection_registry_fix_5/summary.md`.
The clean 448-case aggregate passes in 358.941 seconds at
`c/magma/trace/out/matrix_redstone_piston_pig_fence_wall_registry_full_1/summary.md`,
with 444 strict state rows, four delayed-cactus diagnostics, and 448 exact raw
block gates. Performance guards pass at 4,856 scalar steps/s, 2.93M Blaze
env-ticks/s, and 30.29 CUDA fps. Other non-full collision shapes remain open.

Snow layers, carpet, beds, cake, enchanting tables, daylight detectors, and
end-portal frames now use their exact 1.11.2 collision boxes for represented
piston-pushed living mobs. Coverage includes snow metadata 0-15 with its
one-layer-short collision rule, all carpet colors, both bed parts and every
facing, every cake bite plus the eaten-west-edge negative, both daylight
detector IDs and all power metadata, and the end-frame base plus centered-eye
second box and side-lane discriminator. The deliberate old-magma probe is at
`c/magma/trace/out/redstone_piston_pig_thin_surface_probe_2/summary.md`; all 14
focused cases pass at
`c/magma/trace/out/redstone_piston_pig_thin_surface_fix_2/summary.md`, and the
complete native runtime suite passes. The 462-case aggregate ran in 438.929
seconds at
`c/magma/trace/out/matrix_redstone_piston_pig_thin_surface_full_2/summary.md`.
One pre-existing trapped-chest row missed its Java open input under the loaded
32-client run and passed on a fresh isolated client at
`c/magma/trace/out/redstone_trapped_chest_viewer_power_open_close_retry_thin_surface_1/summary.md`.
Together these provide 458 strict state passes, four delayed-cactus
diagnostics, and 462 exact behavior/raw outcomes. Performance guards pass at
5,052 scalar steps/s on an idle pinned core, 2.93M Blaze env-ticks/s, and 24.63
CUDA fps on an idle pinned core. Rejected samples on occupied sibling/core
affinities are retained as host-contaminated measurements. Other non-full
collision families remain open.

Glass panes, stained panes, iron bars, oak trapdoors, and iron trapdoors now
have exact 1.11.2 static collision shapes in the represented piston path.
Pane coverage includes the permanent center post, all four arms, isolated and
corner negatives, pane/glass/full-cube connections, and the exact side-solid
exceptions for farmland, snow layer 8, bottom slabs, stair sides/shapes, and
redstone blocks. Trapdoor coverage spans both IDs, top/bottom closed panels,
all four open orientations, and opposite-lane negatives. The focused trapdoor
set passed immediately. The pane probe exposed the earlier causal mismatch at
tick 1: Java clipped a piston-pushed item against the pane while magma applied
static-axis clipping only to living mobs. The shared item resolver now performs
that bounded swept-AABB clip, and all eight pane/iron-bar cases pass at
`c/magma/trace/out/redstone_piston_pane_fix_1/summary.md`. The 482-case
aggregate ran in 637.524 seconds with 16 clients at
`c/magma/trace/out/matrix_redstone_piston_pane_trapdoor_full_2/summary.md`.
Two unrelated rows captured stale Java setup state and both pass on immediate
fresh clients at
`c/magma/trace/out/drowning_fire_retry_pane_trapdoor_1/summary.md`, yielding
478 strict rows, four existing diagnostics, and 482 exact behavior/raw
outcomes. The full native runtime suite passes. Performance guards pass at
5,137 scalar steps/s, 2.92M Blaze env-ticks/s, and 30.33 CUDA fps on GPU 1.
The new item clip is entered only for an active item intersected by an active
moving piston; it adds no idle scan.

Cauldron 118 and hopper 154 now use their exact compound base plus four
full-height 1/8 rim boxes in the represented piston path. Ten center/rim Java
cases pass, with the hopper powered from below to disable transfer and isolate
geometry. Empty five-slot hopper state now round-trips through the capsule;
item transfer remains R-05 work and is not claimed. The full native suite
passes. The 16-client aggregate ran 492 cases in 644.095 seconds at
`c/magma/trace/out/matrix_redstone_piston_cauldron_hopper_full_1/summary.md`.
Its sole unrelated comparator flag passed immediately on a fresh client at
`c/magma/trace/out/comparator_extension_retry_cauldron_hopper_1/summary.md`,
yielding 488 strict rows, four existing diagnostics, and 492 exact
behavior/raw outcomes. Performance guards pass at 4,336 scalar steps/s, 2.91M
Blaze env-ticks/s, and 28.91 CUDA fps on GPU 1. Other compound and non-full
static shapes remain open.

Anvil 145, end rod 198, and dragon egg 122 now use their exact 1.11.2 static
collision shapes in the represented piston path. The pre-fix 12-case probe at
`c/magma/trace/out/redstone_piston_item_directional_shape_probe_1/summary.md`
fails only the six occupied lanes at tick 1 by y=0.26 while every empty-lane
control, raw block transition, and light volume passes. Horizontal end rods
also have mid-height EAST-piston pig proofs and vertical/upper controls, so all
three axes are measured rather than inferred from a non-intersecting top pass.
The corrected 16-case composite passes at
`c/magma/trace/out/redstone_piston_directional_shape_fix_2/summary.md` and
`c/magma/trace/out/redstone_piston_end_rod_z_upper_lane_fix_3/summary.md`.
Native coverage includes both anvil axes across all damage classes, all six
rod facings, and the dragon-egg inset; the full runtime suite passes. The clean
16-client aggregate passes 508/508 outcomes in 728.010 seconds at
`c/magma/trace/out/matrix_redstone_piston_directional_shape_full_1/summary.md`:
504 strict state rows plus the four existing diagnostics. Performance guards
pass at 4,201 scalar steps/s, 2.89M Blaze env-ticks/s, and 28.77 CUDA fps on
GPU 1. Other non-full static shapes remain open.

Ordinary chest 54 and trapped chest 146 now use the exact 7/8-high, 1/16-inset
collision box and Java's NORTH, SOUTH, WEST, EAST same-registry neighbor
priority. Ordinary and trapped chests do not join one another. The pre-fix
nine-case probe at
`c/magma/trace/out/redstone_piston_item_chest_shape_probe_1/summary.md`
isolates the first causal divergence at tick 1: every occupied surface is
0.135 blocks too low in magma while the empty-lane controls and all raw block
and light cells pass. The corrected focused set passes 9/9 at
`c/magma/trace/out/redstone_piston_chest_shape_fix_2/summary.md`, and the full
native runtime suite passes. The clean 16-client aggregate passes 517/517 in
769.246 seconds at
`c/magma/trace/out/matrix_redstone_piston_chest_shape_full_1/summary.md`: 513
strict state rows plus the four existing diagnostics, with no retry.
Performance recapture is pending rather than promoted under three unrelated
Tak generators occupying 176 workers. The failed samples are retained at
`c/magma/trace/out/perf_guard_redstone_chest_static_shape_1.json` through
`perf_guard_redstone_chest_static_shape_4.json`; their CPU and render binaries
are byte-identical to the preceding clean 4,201/28.77 evidence, while the
batched CUDA metric still passes at 2.93M env-ticks/s.

All seven 1.11.2 door block IDs now use paired lower/upper actual state for
their exact 3/16-thick collision panel. Lower metadata supplies facing and
open state; upper metadata supplies hinge, including the facing/hinge-dependent
rotation of an open panel. The pre-fix nine-case probe at
`c/magma/trace/out/redstone_piston_item_door_shape_probe_1/summary.md` fails
all seven occupied panels at tick 1 by y=0.26 while both empty-lane controls
and every raw block/light cell pass. All nine focused cases pass at
`c/magma/trace/out/redstone_piston_door_shape_fix_1/summary.md`; native tests
cover all four closed facings, all eight open facing/hinge combinations, lane
negatives, and representative states for every door ID. The full native suite
passes. The 526-case aggregate ran in 3,822.606 seconds with four clients at
`c/magma/trace/out/matrix_redstone_piston_door_shape_full_3/summary.md`. Its
one older trapped-chest Java capture miss passes on a fresh isolated client at
`c/magma/trace/out/redstone_trapped_chest_viewer_power_open_close_retry_door_shape_1/summary.md`,
yielding 522 strict rows, four existing diagnostics, and 526 exact
behavior/raw outcomes. Performance recapture remains pending under unrelated
host CPU saturation; the new branch is reached only for an active
piston/item collision and adds no scan, allocation, or idle work.

Cactus 81, lily pad 111 for non-boats, ender chest 130, flower pot 140,
skull 144, ladder 65, and cocoa 127 now use their exact 1.11.2 static collision
boxes in the represented piston path. Skull and ladder metadata retain their
distinct facing decoders; cocoa covers every facing and age-dependent size.
The corrected pre-fix probes isolate eight occupied-geometry mismatches while
the eight exact empty-space controls and all raw block/light outcomes pass:
`c/magma/trace/out/redstone_piston_item_remaining_shape_probe_1/summary.md`,
`redstone_piston_item_remaining_shape_probe_2/summary.md`,
`redstone_piston_item_remaining_shape_probe_4/summary.md`, and
`redstone_piston_flower_pot_shape_probe_1/summary.md`. All 16 corrected focused
cases pass at
`c/magma/trace/out/redstone_piston_remaining_shape_fix_1/summary.md`; native
coverage sweeps cactus/ender-chest footprints, lily/skull/pot heights, all
wall-skull and ladder facings, all 12 valid cocoa facing/age states, and their
negative lanes. The complete native runtime suite passes. The four-client
aggregate passes 542/542 in 4,519.773 seconds at
`c/magma/trace/out/matrix_redstone_piston_remaining_shape_full_1/summary.md`:
538 strict rows and the same four delayed-cactus diagnostics, with no retry or
infrastructure failure. Performance recapture is pending because unrelated
Tak generators and search workers saturate the host CPUs. The new branches
remain bounded to active piston/entity intersections and add no scan,
allocation, or idle-world hook.

Chorus plant 199 now uses Java's actual-state collision: a centered 5/8 cube
plus a 3/16 arm toward every adjacent chorus plant or flower, with DOWN also
connecting to end stone. The clean three-case pre-fix proof at
`c/magma/trace/out/redstone_piston_chorus_plant_shape_probe_3/summary.md`
fails the occupied center and connected north arm at tick 1 by exactly 0.0725,
while the isolated north lane and every raw block/light outcome pass. Earlier
candidate fixtures were rejected because a downward head invalidated a second
plant or a full-cube flower contaminated the arm lane. The corrected focused
set passes 3/3 at
`c/magma/trace/out/redstone_piston_chorus_plant_shape_fix_1/summary.md`.
Native coverage locks the center, all four horizontal arms, UP and DOWN arms,
and their exact empty lanes; the complete runtime suite passes. The
four-client aggregate passes 545/545 in 3,011.750 seconds at
`c/magma/trace/out/matrix_redstone_piston_chorus_plant_shape_full_1/summary.md`:
541 strict rows and the same four delayed-cactus diagnostics, with no retry or
infrastructure failure. Performance recapture is pending under unrelated CPU
saturation. The seven-box maximum is fixed stack storage reached only during
an active piston/entity intersection, with no allocation or idle-world hook.

Farmland 60 and grass path 208 now use the exact full-footprint 15/16-high
collision box from their local 1.11.2 Java sources. The clean four-case
pre-fix proof at
`c/magma/trace/out/redstone_piston_farmland_grass_path_shape_probe_1/summary.md`
fails both occupied side lanes first at tick 1 by exactly 0.26 item X while
both just-above controls and every raw block/light outcome pass. The farmland
fixture uses moisture 0 beneath mature wheat, preventing both drying and
growth without adding collision. All four corrected cases pass at
`c/magma/trace/out/redstone_piston_farmland_grass_path_shape_fix_1/summary.md`.
Native coverage checks all eight valid farmland moisture states and grass path
on both sides of the 15/16 boundary; the complete runtime suite passes. The
four-client sweep ran all 549 cases in 2,751.851 seconds at
`c/magma/trace/out/matrix_redstone_piston_farmland_grass_path_shape_full_1/summary.md`.
Two older rows exposed fixture contamination: a final-staged trapped chest was
not yet present in the Java client ray trace at its click, and an ordinary
daylight detector crossed its ambient 20-tick update and changed metadata 0 to
15. Staging the chest early and starting the frozen-noon detector at metadata
15 make both probes phase-independent. Those rows plus all four new cases pass
6/6 at
`c/magma/trace/out/redstone_piston_farmland_grass_path_shape_fixture_hardening_1/summary.md`,
yielding a composite 545 strict rows plus the same four delayed-cactus
diagnostics and 549/549 behavior/raw outcomes. The performance guard passes at
`c/magma/trace/out/perf_guard_redstone_piston_farmland_grass_path_shape_1.json`:
4,990 scalar steps/s, 2.93M Blaze env-ticks/s, and 30.85 CUDA fps on GPU 1. The
new shape branch is bounded to active piston/entity intersection work and adds
no scan, allocation, or idle-world hook.

Repeater IDs 93/94 and comparator IDs 149/150 now inherit the exact
`BlockRedstoneDiode.REDSTONE_DIODE_AABB`: a full-footprint box with height
1/8. Before the fix, a downward piston pushed a NoAI pig through both block
families to Y `79.09000002384185`; Java stops it at Y `79.125`. Both old-C
failures isolate that one entity coordinate while moving-piston state,
comparator output tile, raw blocks, block light, queues, and RNG remain exact
at
`c/magma/trace/out/matrix_redstone_piston_diode_shape_probe_1/summary.md`.
The corrected repeater/comparator pair passes 2/2 with 25 matching state
features and all 10,625 block/light cells exact at
`c/magma/trace/out/matrix_redstone_piston_diode_shape_candidate_1/summary.md`.
Native coverage exhausts all 16 metadata values for all four IDs, and the
selection box is mirrored in CPU and CUDA. The complete runtime suite passes
in 4:28 with a 285 MiB peak and zero swap at
`c/magma/trace/out/test_runtime_redstone_piston_diode_shape.log`. GPU 1
performance passes at
`c/magma/trace/out/perf_guard_redstone_piston_diode_shape.json`: 5,070 scalar
steps/s, 2.94M Blaze env-ticks/s, and 31.20 1080p CUDA fps. The collision
branch is reached only for an active piston/entity intersection and adds no
idle scan or allocation.

Brewing stand 117 now uses Java's exact compound collision geometry: the
centered `STICK_AABB` spans 7/16..9/16 in X/Z and reaches height 7/8, followed
by the full-footprint 1/8-high `BASE_AABB`. Its selection box is the base only,
mirrored in CPU and CUDA. The deliberate old-magma pair passes every raw
block/light comparison but pushes both center and side-lane pigs through to Y
`79.09000002384185` at
`c/magma/trace/out/matrix_redstone_piston_brewing_stand_shape_probe_3/summary.md`.
The corrected pair matches Java's center sequence
`80 -> 79.875 -> 79.875` and side sequence
`80 -> 79.59000002384185 -> 79.125` at
`c/magma/trace/out/matrix_redstone_piston_brewing_stand_shape_candidate_1/summary.md`.
These historical rows remain state-diagnostic because their saved fixtures
predate the now-represented brewing inventory/timer state. Native coverage
checks both lanes for all eight bottle-bit states, and the complete suite
passes in 5:19 with a 286 MiB peak and zero swap at
`c/magma/trace/out/test_runtime_brewing_stand_shape.log`. GPU 1 performance
passes at `c/magma/trace/out/perf_guard_brewing_stand_shape.json`: 4,329 scalar
steps/s, 2.92M Blaze env-ticks/s, and 27.05 1080p CUDA fps. The branch runs
only while resolving an active static collision and adds no scan or heap
allocation.

Normal and sticky piston bases 33/29 now have exact static geometry.
Retracted bases are full cubes; extended bases retain the facing-opposite 3/4
body from `BlockPistonBase`. The same six-facing boxes drive CPU and CUDA
selection. Old magma pushes a pig through both a retracted normal base and an
extended-UP sticky base to Y `79.09000002384185`, while Java stops at Y `80`
and `79.75`; those are the only simulated divergences and every raw block/light
cell is exact at
`c/magma/trace/out/matrix_redstone_piston_base_shape_probe_1/summary.md`.
Both corrected strict rows pass at
`c/magma/trace/out/matrix_redstone_piston_base_shape_candidate_1/summary.md`.
Native coverage spans both IDs, all six valid facings, retracted/extended
bodies, and all four horizontal empty quarters. The full runtime suite passes
in 6:05 with a 287 MiB peak and zero swap at
`c/magma/trace/out/test_runtime_redstone_piston_base_shape.log`.

Closed shulker boxes 219..234 now use their full-cube collision for the
represented closed tile state. In the old-C downward-piston fixture Java stops
the pig at Y `80`, while magma reaches Y `79.09000002384185`; exact container
NBT, raw blocks, block light, queues, and RNG isolate collision as the only
cause at
`c/magma/trace/out/matrix_redstone_piston_closed_shulker_shape_probe_1/summary.md`.
The corrected strict row passes at
`c/magma/trace/out/matrix_redstone_piston_closed_shulker_shape_candidate_1/summary.md`.
Selection coverage spans all 16 colors and six valid facings. Animated lid
extension and entity pushing while open remain outside this closed-state claim.

Ordinary player movement now uses the same full-footprint, 1/8-high collision
for repeaters 93/94 and comparators 149/150. The fixture inserts a supported
repeater after the player leaves the floor, then applies one exact jump. Java
reaches the repeater surface at Y `78.125` on tick 10 and reports grounded on
tick 11; old magma falls through to the platform at Y `78`. The non-vacuous old
behavior fails only on magma at
`c/magma/trace/out/matrix_ordinary_player_diode_landing_probe_behavior_2/summary.md`.
The corrected strict row passes with 25 matching state features, zero
divergences, and all 10,625 raw block/light cells exact at
`c/magma/trace/out/matrix_ordinary_player_diode_landing_candidate_1/summary.md`.
The affected piston-player, tripwire-player, and pressure-plate-player family
passes 4/4 at
`c/magma/trace/out/matrix_ordinary_player_diode_landing_affected_1/summary.md`.
Native coverage exhausts all 64 diode ID/metadata states and a collision-free
wire control; the shared CPU/CUDA driver agrees on all 2,432 output lines.

The combined full runtime suite passes in 5:51 with a 287 MiB peak and zero
swap at
`c/magma/trace/out/test_runtime_piston_base_closed_shulker_player_diode.log`.
After the unrelated 56-thread load ended, GPU 1 performance passed at
`c/magma/trace/out/perf_guard_piston_base_closed_shulker_player_diode.json`:
4,636 scalar steps/s, 2.92M Blaze env-ticks/s, and 28.19 1080p CUDA fps. This
promotes the two piston-base rows, one closed-shulker row, and one ordinary
player-diode row, bringing the composite behavior/raw total to 657.

The next ordinary-player collision batch is performance-promoted. Brewing
stand 117 now contributes its centered 7/8 stem and full-footprint 1/8 base to
the shared movement broadphase; enchanting table 116 is 3/4 high; farmland 60
and grass path 208 are 15/16 high. Single slabs 44/126/182/205 preserve their
top/bottom half, carpet 171 is 1/16 high, snow 78 uses metadata heights from
zero through 7/8, and cake 92 preserves its bitten inset half-height box. The
deliberate old rows isolate the
first landing-coordinate divergence at
`c/magma/trace/out/matrix_ordinary_player_brewing_stand_landing_probe_behavior_1/summary.md`,
`c/magma/trace/out/matrix_ordinary_player_enchanting_table_landing_probe_behavior_1/summary.md`,
and
`c/magma/trace/out/matrix_ordinary_player_farmland_grass_path_landing_probe_behavior_1/summary.md`.
The corrected rows pass at
`c/magma/trace/out/matrix_ordinary_player_brewing_stand_landing_candidate_1/summary.md`,
`c/magma/trace/out/matrix_ordinary_player_enchanting_table_landing_candidate_1/summary.md`,
and
`c/magma/trace/out/matrix_ordinary_player_farmland_grass_path_landing_candidate_1/summary.md`.
The slab row passes at
`c/magma/trace/out/matrix_ordinary_player_bottom_slab_landing_candidate_1/summary.md`,
and carpet, snow, and cake pass at
`c/magma/trace/out/matrix_ordinary_player_carpet_snow_cake_landing_candidate_1/summary.md`.
Brewing has one historical container-completeness diagnostic recorded before
the active F-03 tile implementation; its physics, behavior, raw blocks, and
block light pass. The other three rows have 25
matching features and zero divergences. The eight-case affected family passes
at
`c/magma/trace/out/matrix_ordinary_player_fixed_surface_shapes_affected_1/summary.md`.
The expanded 12-case affected family passes at
`c/magma/trace/out/matrix_ordinary_player_thin_surface_shapes_affected_1/summary.md`.
Focused native tests cover every represented metadata state and both brewing
lanes; CPU and CUDA agree on all 2,432 player-survival outputs. The full native
aggregate passes in 4:56 with a 288 MiB peak and zero swap at
`c/magma/trace/out/test_runtime_ordinary_thin_surface_shapes.log`. The clean
GPU 1 guard passes at
`c/magma/trace/out/perf_guard_ordinary_thin_surface_shapes.json`: 4,671 scalar
steps/s, 2.93M Blaze env-ticks/s, and 24.38 1080p CUDA fps. These eight
outcomes bring the promoted behavior/raw total to 665.

The next ordinary-player shape batch is performance-promoted. Beds 26 use the
shared 9/16 box; daylight detectors
151/178 use 3/8; end portal frames 120 retain their 13/16 base plus optional
center eye; ender chests 130 use the inset 7/8 box; trapdoors 96/167 retain all
open/top/bottom 3/16 panels; and chorus plants 199 use the centered 5/8 cube
plus actual-state arms, including the end-stone DOWN exception. The deliberate
old rows fail their exact landing behavior with exact raw block/light state at
`c/magma/trace/out/matrix_ordinary_player_bed_daylight_frame_ender_chest_probe_behavior_1/summary.md`
and
`c/magma/trace/out/matrix_ordinary_player_trapdoor_chorus_probe_behavior_1/summary.md`.
All six corrected rows are strict, and the affected family passes 12/12 at
`c/magma/trace/out/matrix_ordinary_player_bed_daylight_frame_ender_chest_trapdoor_chorus_affected_1/summary.md`.
Focused native tests exhaust represented metadata, optional-eye, panel, and all
six chorus-arm states. CPU and CUDA agree on all 2,432 outputs. The full native
suite passes under elevated host load in 6:24 with a 288 MiB peak and zero swap
at
`c/magma/trace/out/test_runtime_ordinary_bed_daylight_frame_ender_chest_trapdoor_chorus.log`.
After the unrelated 56-thread stage ended, the clean GPU 1 guard passed at
`c/magma/trace/out/perf_guard_ordinary_bed_daylight_frame_ender_chest_trapdoor_chorus.json`:
4,765 scalar steps/s, 2.93M Blaze env-ticks/s, and 30.50 1080p CUDA fps. These
six strict outcomes bring the promoted total to 671.

Cauldron 118, hopper 154, and flower pot 140 are performance-promoted for
ordinary players. Cauldron and hopper use their exact five-box base
and rim collision; flower pot uses its centered 3/8-high box. Live hopper and
flower-pot placement now also creates the empty tile immediately. The causal
hopper/pot rerun proves tile state exact before the remaining collision
failure, and the final three corrected rows are strict at
`c/magma/trace/out/matrix_ordinary_player_cauldron_hopper_flower_pot_candidate_1/summary.md`.
The nine-case affected family passes at
`c/magma/trace/out/matrix_ordinary_player_cauldron_hopper_flower_pot_affected_1/summary.md`;
CPU/CUDA agree on all 2,432 player-survival outputs. The combined final-source
native suite passes in 5:45 with a 288 MiB peak and zero swap at
`c/magma/trace/out/test_runtime_ordinary_cactus_collision_damage.log`, and the
clean GPU 1 guard passes at
`c/magma/trace/out/perf_guard_ordinary_cauldron_hopper_flower_pot_cactus.json`:
4,775 scalar steps/s, 2.93M Blaze env-ticks/s, and 30.67 1080p CUDA fps.

Cactus 81 now has exact ordinary-player collision and contact damage. Its box
is inset 1/16 horizontally and 15/16 high; each contracted-box callback enters
the existing one-point armor/hurt-resistance path and adds the default 0.1
exhaustion only when accepted. The deliberate old-C jump proof fails damage at
tick 1 and later lands at Y 79 on a full cube. The causal damage-only rerun
makes tick-1 health, hurt time, and exhaustion exact before geometry remains;
the final stationary row strictly reproduces damage at ticks 1/11/21 and
natural regeneration at ticks 10/20. The jump row proves inset landing/contact
on both sides but remains state-diagnostic: Java's post-damage self-velocity
packet arrived at tick 3 in two captures and tick 4 in another, changing the
client trajectory while server state stayed deterministic. The 12-case
affected family passes at
`c/magma/trace/out/matrix_ordinary_player_cactus_affected_1/summary.md`, native
coverage exhausts all 16 metadata states, CPU/CUDA agree on all 2,432 outputs,
and the aggregate/performance evidence is at the current paths and rates above.
Broader deterministic modeling of asynchronous client packet delivery remains
open; no C trajectory was fitted to one race outcome.

Horizontal end rods 198 and skulls 144 now use exact metadata-derived player
collision. The old-C probe first fails end-rod physics at tick 8 and skull tile
creation at tick 1, then the old full-cube skull shape. Live skull placement
now creates the exact default tile; its causal rerun makes tile state exact and
moves the first divergence to tick-8 physics. Both corrected strict rows pass
at
`c/magma/trace/out/matrix_ordinary_player_end_rod_skull_candidate_1/summary.md`,
and their 12-case affected family passes at
`c/magma/trace/out/matrix_ordinary_player_end_rod_skull_affected_1/summary.md`.

Lily pads 111 now use the non-boat inset 3/32-high box, and ordinary/trapped
chests 54/146 use their same-registry joined, inset 7/8 box. Live chest block
insertion also materializes its empty 27-slot tile. The old-C lily proof falls
through to the water basin at tick 11. Both chest proofs first lack the tile at
tick 1 and then land 1/8 too high at tick 8. The causal tile-only rerun makes
both container lists exact before geometry remains. All three final rows are
strict at
`c/magma/trace/out/matrix_ordinary_player_lily_chests_candidate_2/summary.md`.
The combined 18-case affected family passes at
`c/magma/trace/out/matrix_ordinary_player_lily_chests_affected_1/summary.md`;
native tests cover all metadata, all four same-registry joins, and the
cross-registry negative. CPU/CUDA agree on all 2,432 player-survival outputs at
`c/magma/trace/out/cpu_cuda_player_survival_end_rod_skull_lily_chests.log`, and
the exact-source native suite passes in 6:27 with a 289 MiB peak and zero swap
at `c/magma/trace/out/test_runtime_ordinary_lily_chests_final.log`.
Performance promotion is pending because GPU 1 is currently shared; no
throughput result from that interval is admitted.

All 14 stair block IDs now use Java's actual-state straight, inner-left,
inner-right, outer-left, and outer-right collision in ordinary-player movement.
The deliberate old-C tape first drives north and then brakes until the complete
player box is over the low half of a bottom stair. Java reaches Y 78.5 on tick
9 and grounds on tick 10; old magma lands on its full cube at Y 79 on tick 8,
with all raw block/light state exact at
`c/magma/trace/out/matrix_ordinary_player_stair_probe_3/summary.md`. The
corrected strict row passes at
`c/magma/trace/out/matrix_ordinary_player_stair_candidate_1/summary.md`, and
the 15-case affected family passes at
`c/magma/trace/out/matrix_ordinary_player_stair_affected_1/summary.md`.
Focused native tests cover every stair registry ID, metadata half/facing, and
all four actual-state corners. CPU/CUDA agree on all 2,432 outputs at
`c/magma/trace/out/cpu_cuda_player_survival_ordinary_stairs.log`; the native
suite passes in 5:09 with a 289 MiB peak and zero swap at
`c/magma/trace/out/test_runtime_ordinary_stairs.log`. Performance remains
pending while the unrelated GPU 1 process holds 100% utilization.

Iron bars 101, glass panes 102, and stained panes 160 now use their exact
actual-state post-and-arm collision in ordinary-player movement. The old-C
north-approach proof isolates the silhouette: Java advances to
Z 7.862500011920929 and stops on tick 4, while magma's old full-cube fallback
stops at Z 8.300000011920929 on tick 1. Player physics is the only simulated
state divergence; raw blocks and block light are exact at
`c/magma/trace/out/matrix_ordinary_player_glass_pane_approach_probe_1/summary.md`.
The corrected approach and centered control pass strict at
`c/magma/trace/out/matrix_ordinary_player_glass_pane_candidate_1/summary.md`,
and all 14 affected pane/piston/stair/player-trigger rows pass at
`c/magma/trace/out/matrix_ordinary_player_glass_pane_affected_1/summary.md`.
Focused native tests cover all three pane IDs, all metadata, four arms,
pane/glass/full-cube neighbors, Forge farmland/snow/redstone side-solid
exceptions, and actual-state stair-side positives and negatives. CPU/CUDA
agree on all 2,432 outputs at
`c/magma/trace/out/cpu_cuda_player_survival_ordinary_panes.log`; the aggregate
suite passes in 4:41 with a 289 MiB peak and zero swap at
`c/magma/trace/out/test_runtime_ordinary_panes.log`. Performance promotion is
pending because GPU 1 remains shared.

Piston bases 29/33, anvils 145, and dragon eggs 122 now use their exact static
collision in ordinary-player movement. Both piston IDs are full cubes while
retracted and lose the facing quarter while extended; anvils select the inset
axis from metadata; dragon eggs keep their 1/16 horizontal inset. The final
fixtures pre-stage valid save states so Java's client and magma begin with the
same obstacle rather than racing a live block-update packet. With the three
new collector branches disabled, old magma stops every approach at full-cube
Z 8.300000011920929 on tick 1 while raw blocks and light remain exact at
`c/magma/trace/out/matrix_ordinary_player_piston_base_anvil_dragon_egg_probe_static_old_1/summary.md`.
The corrected rows reach Java's exact faces and pass strict at
`c/magma/trace/out/matrix_ordinary_player_piston_base_anvil_dragon_egg_candidate_static_2/summary.md`.
All 16 affected player, piston-shape, mobility, and DESTROY rows pass at
`c/magma/trace/out/matrix_ordinary_player_piston_base_anvil_dragon_egg_affected_1/summary.md`.
Native coverage exhausts both piston IDs and all metadata, all anvil
damage/facing states, and all dragon-egg metadata. CPU/CUDA agree on 2,432
outputs at
`c/magma/trace/out/cpu_cuda_player_survival_ordinary_piston_base_anvil_dragon_egg.log`;
the aggregate passes in 4:52 with a 289 MiB peak and zero swap at
`c/magma/trace/out/test_runtime_ordinary_piston_base_anvil_dragon_egg.log`.
Performance remains pending while GPU 1 is shared.

All seven fence IDs 85/113/188..192 and six fence-gate IDs 107/183..187 now
use exact ordinary-player collision. Fences keep a 1.5-high center post plus
actual-state arms; wood variants connect across wood species but not to nether
brick, while gates and exact opaque full cubes connect subject to Java's
barrier/gourd exclusions. Closed gates use the metadata axis at height 1.5 and
open gates are collision-free. Old magma treats the staged closed oak gate,
open oak gate, and isolated spruce fence as full cubes, first diverging only
in player physics at tick 1 with exact blocks/light at
`c/magma/trace/out/matrix_ordinary_player_fence_gate_fence_probe_1/summary.md`.
All three corrected strict rows pass at
`c/magma/trace/out/matrix_ordinary_player_fence_gate_fence_candidate_1/summary.md`,
and all 16 affected player/piston/wall/support rows pass at
`c/magma/trace/out/matrix_ordinary_player_fence_gate_fence_affected_1/summary.md`.
Native coverage exhausts every fence/gate ID and metadata, all arms,
wood/nether and barrier/gourd controls, and the exact 4,096-state
opaque-neighbor projection. CPU/CUDA agree on 2,432 outputs at
`c/magma/trace/out/cpu_cuda_player_survival_ordinary_fences_gates.log`; the
aggregate passes in 4:41 with a 289 MiB peak and zero swap at
`c/magma/trace/out/test_runtime_ordinary_fences_gates.log`. Performance remains
pending on the shared GPU.

All seven door IDs 64/71/193..197, ladders 65, and cocoa pods 127 now use
exact ordinary-player collision. Door halves derive one actual panel from the
lower facing/open state and upper hinge; cocoa derives its attached pod from
age and facing. The deliberate old-C four-case approach treats every block as
a full cube and first diverges at tick 1 with exact raw blocks and light at
`c/magma/trace/out/matrix_ordinary_player_door_ladder_cocoa_probe_1/summary.md`.
The corrected closed door reaches Z 7.487500011920929 on tick 5, the open-door
center lane remains collision-free, and age-2 cocoa reaches
Z 7.862500011920929 on tick 4.

That geometry proof exposed a separate ladder-travel omission at tick 4.
The shared CPU/CUDA player kernel now implements 1.11.2's ladder/vine motion
clamp, fall-distance reset, horizontal-collision climb impulse, and the Forge
matching-open-trapdoor-over-ladder rule. The exact 20-tick ladder trace passes
at `c/magma/trace/out/matrix_ordinary_player_ladder_candidate_3/summary.md`.
All four strict rows plus 14 neighboring door/trapdoor/ladder/cocoa controls
pass 18/18 at
`c/magma/trace/out/matrix_ordinary_player_doors_ladder_cocoa_affected_1/summary.md`.
Native coverage exhausts all paired door state, ladder/cocoa metadata, ladder
identity, and the exact climb/release trace. CPU/CUDA agree on all 2,432
outputs at
`c/magma/trace/out/cpu_cuda_player_survival_ordinary_doors_ladder_cocoa.log`;
the aggregate passes in 4:25 with a 289 MiB peak and zero swap at
`c/magma/trace/out/test_runtime_ordinary_doors_ladder_cocoa.log`. Performance
promotion remains pending while GPU 1 is shared; no timing from this interval
is admitted.

MobEffects.LEVITATION 25 now affects ordinary player travel. The active effect
replaces gravity with vanilla's `motionY += (0.05 * (amplifier + 1) -
motionY) * 0.2` before the shared 0.98 drag; duration-zero removal restores
gravity before that tick's movement. The old-C duration-10 fixture keeps exact
potion state but remains grounded, first diverging at tick 0 while blocks and
light stay exact at
`c/magma/trace/out/matrix_ordinary_player_levitation_probe_1/summary.md`.
The corrected row reproduces the exact tick-0 0.0098 rise velocity, tick-8
Y 78.22179048926562, tick-9 expiry/apex, fall-distance accumulation, and
tick-12 landing at
`c/magma/trace/out/matrix_ordinary_player_levitation_candidate_2/summary.md`.
Random movement seeds, drowning, surface reset, speed-potion expiry, and
ladder travel pass 7/7 with it at
`c/magma/trace/out/matrix_ordinary_player_levitation_affected_1/summary.md`.

The focused native effect test is now a fail-fast precheck in
`c/magma/game/test_runtime.sh`. The optional player-survival driver argument
also activates levitation in both builds; CPU/CUDA agree on all 2,432 emitted
values at
`c/magma/trace/out/cpu_cuda_player_survival_ordinary_levitation.log`. The full
aggregate passes with a 289 MiB peak and zero swap at
`c/magma/trace/out/test_runtime_ordinary_levitation_final.log`. Its wall time
is excluded under current host contention, and performance promotion remains
pending while GPU 1 is shared.

MobEffects.JUMP_BOOST 8 now affects ordinary-player jump travel and the
integrated server's movement-packet prediction. The initial vertical impulse
is vanilla's 0.41999998688697815 plus float `0.1 * (amplifier + 1)`. In the
amplifier-1 fixture, old magma performs the basic jump and lands at tick 11,
while Java starts at Y 78.61999998986721 with motion 0.5292000003695486 after
drag, reaches Y 80.51679398321414 at tick 7, touches Y 78 at tick 15, and
reports authoritative ground at tick 16. The deliberate old result is at
`c/magma/trace/out/matrix_ordinary_player_jump_boost_probe_1/summary.md`.

The exact 30-tick arc, landing boundary, exhaustion increment, and potion
duration 39 through 10 pass at
`c/magma/trace/out/matrix_ordinary_player_jump_boost_candidate_4/summary.md`.
Both random movement seeds, speed-potion expiry, levitation, cake landing, and
ladder travel pass 7/7 with it at
`c/magma/trace/out/matrix_ordinary_player_jump_boost_affected_1/summary.md`.
The fail-fast native effect gate checks both the client impulse and delayed
server prediction. The optional driver argument activates amplifier 1 in both
compiled kernels; CPU/CUDA agree on all 2,432 values at
`c/magma/trace/out/cpu_cuda_player_survival_ordinary_jump_boost_final.log`.
Jump Boost also subtracts `amplifier + 1` before vanilla's fall-damage ceil.
The isolated 1.11.2 Java formula agrees with CPU and CUDA on 400 rows at
`c/magma/trace/out/java_cpu_cuda_player_vitals_jump_boost_ii.log`, while the
native runtime drop checks four boosted damage against six ordinary damage.
GPU performance promotion remains pending while GPU 1 is shared.

MobEffects.WATER_BREATHING 13 now suppresses submerged air loss while active.
The effect is checked before potion duration aging, so duration 1 protects the
current drowning pass, is removed later in that tick, and ordinary air loss
resumes on the following observation. The old implementation retained exact
potion state but decremented air immediately, first diverging at tick 0 at
`c/magma/trace/out/matrix_ordinary_player_water_breathing_probe_1/summary.md`.
The exact hold/expiry/resume trace passes at
`c/magma/trace/out/matrix_ordinary_player_water_breathing_candidate_1/summary.md`.
Both random seeds, the full drowning and surface-air lifecycles, Speed expiry,
and Levitation pass 7/7 with it at
`c/magma/trace/out/matrix_ordinary_player_water_breathing_affected_1/summary.md`.
The scan is bounded by the fixed potion list and runs only for an eye inside a
water block; the dry base path is unchanged. GPU performance promotion remains
pending while GPU 1 is shared. The combined Jump Boost and Water Breathing
aggregate passes with a 296,036 KB peak and zero swap at
`c/magma/trace/out/test_runtime_ordinary_jump_boost_water_breathing_final.log`;
its contended wall time is not admitted as performance evidence.

MobEffects.FIRE_RESISTANCE 12 now rejects ordinary-player scheduled burn
damage and fire-block contact damage while active without stopping the fire
counter itself. Damage is evaluated before potion duration aging, so duration
1 suppresses the scheduled hit on the expiry tick. Old magma accepts that hit
at tick 2 despite retaining the exact potion list, first diverging in health,
exhaustion, and hurt state at
`c/magma/trace/out/matrix_ordinary_player_fire_resistance_probe_1/summary.md`.
The corrected trace remains at health 20 through tick 21 and accepts the next
scheduled burn at tick 22, passing strict at
`c/magma/trace/out/matrix_ordinary_player_fire_resistance_candidate_1/summary.md`.
Both random seeds, the existing fire counter, contact, and extinguish cases,
and Water Breathing pass 7/7 at
`c/magma/trace/out/matrix_ordinary_player_fire_resistance_affected_1/summary.md`.
The fixed-size potion lookup is entered only when contact or the 20-tick burn
schedule attempts damage. Other fire sources remain separate parity slices.
GPU performance promotion remains pending while GPU 1 is shared.

MobEffects.HUNGER 17 now performs its amplifier-scaled exhaustion action on
every active tick before duration aging. Old magma preserves the Hunger II
duration list but leaves exhaustion at its captured baseline, first diverging
at tick 0 at
`c/magma/trace/out/matrix_ordinary_player_hunger_probe_1/summary.md`. The
corrected trace adds float 0.01 on each of three active ticks, including the
expiry tick, and remains unchanged afterward at
`c/magma/trace/out/matrix_ordinary_player_hunger_candidate_1/summary.md`.
Both random controls and all neighboring potion and jump-exhaustion rows pass
8/8 at `c/magma/trace/out/matrix_ordinary_player_hunger_affected_1/summary.md`.

MobEffects.POISON 19 now uses vanilla's `25 >> amplifier` readiness cadence,
applies one point of armor-bypassing magic damage through the ordinary hurt
gate, and stops at one health. The short obtainable Poison II fixture begins
at duration 12 to isolate one non-vacuous hit from natural regeneration. Old
magma first diverges in health and hurt state at tick 0 at
`c/magma/trace/out/matrix_ordinary_player_poison_probe_2/summary.md`; the exact
duration-11-through-8, hurt-10-through-7 trace passes at
`c/magma/trace/out/matrix_ordinary_player_poison_candidate_1/summary.md`.
The affected random, drowning, melee, fire, and potion family passes 10/10 at
`c/magma/trace/out/matrix_ordinary_player_poison_affected_1/summary.md`.
Hunger and Poison add no scan: both actions execute inside the pre-existing
fixed active-potion loop. The combined Fire Resistance, Hunger, and Poison
aggregate passes with a 295,928 KB peak and zero swap at
`c/magma/trace/out/test_runtime_ordinary_fire_resistance_hunger_poison_final.log`.
Its shared-host wall time is excluded, and GPU performance promotion remains
pending while GPU 1 is shared.

MobEffects.HASTE 3 and MINING_FATIGUE 4 now feed their active amplifiers into
the ordinary player-break input and their operation-2 modifiers into the
attack-speed attribute. Old magma keeps Haste II duration exact but reports
the base 0.20 cooldown instead of 0.24 and leaves a staged hand-mined stone
intact over 120 ticks at
`c/magma/trace/out/matrix_ordinary_player_haste_mining_probe_1/summary.md`.
The corrected state trace and raw stone-to-air transition pass at
`c/magma/trace/out/matrix_ordinary_player_haste_mining_candidate_1/summary.md`.

The paired Mining Fatigue I row keeps that stone intact through tick 179 and
matches the 0.18 cooldown and all 180 potion rows at
`c/magma/trace/out/matrix_ordinary_player_mining_fatigue_candidate_1/summary.md`.
The no-effect mining case breaks the same stone within the window, providing
the non-vacuous control. Both random controls, ordinary mining, Speed II,
baseline melee, Strength, Weakness, Haste, and Fatigue pass 9/9 at
`c/magma/trace/out/matrix_ordinary_player_haste_fatigue_affected_1/summary.md`.
The attributes refresh only when the bounded potion list changes, and mining
reuses its existing constant-time amplifier branches. Native player-effect
and player-control gates pass. The CPU aggregate passes with a 295,800 KB peak
and zero swap at
`c/magma/trace/out/test_runtime_ordinary_haste_fatigue_final.log`. GPU timing
and performance promotion remain deferred while GPU 1 is shared.

MobEffects.RESISTANCE 11 now applies its amplifier-scaled 20-percent reduction
after armor in the shared ordinary contact, mob, magic, and explosion damage
paths. The focused cactus row keeps 24 of 25 simulated features and raw blocks
exact in old magma but takes one full damage point instead of Java's 0.8 at
tick 1 at
`c/magma/trace/out/matrix_ordinary_player_resistance_probe_1/summary.md`.
The corrected four-tick state, potion, block, and light trace passes at
`c/magma/trace/out/matrix_ordinary_player_resistance_candidate_1/summary.md`.
Nine affected rows passed before the matrix's existing fire-contact behavior
checker raised on a missing local baseline; after restoring that assertion,
the interrupted fire row passes at
`c/magma/trace/out/matrix_ordinary_player_resistance_fire_contact_repair_1/summary.md`.
The native effect and mob suites pass. The mob suite also now stages its gold
weighted-plate control inside the loaded range with valid stone support,
instead of silently testing air. The CPU aggregate passes with a 296,616 KB
peak and zero swap at
`c/magma/trace/out/test_runtime_ordinary_resistance_final.log`. Shared-host
timing and GPU performance promotion are excluded while GPU 1 is shared.

MobEffects.ABSORPTION 22 now grants four gold-heart points per amplifier level,
consumes them after armor and Resistance but before health, and subtracts the
original modifier amount on expiry or removal. The focused duration-3 cactus
row proves both sides of the boundary: Java consumes one gold-heart point with
no health or exhaustion loss at tick 1, removes the remaining three at tick 2,
then takes one health point and 0.1 exhaustion on the unprotected tick-11 hit.
Old magma loses health at the first hit at
`c/magma/trace/out/matrix_ordinary_player_absorption_probe_1/summary.md`.
The corrected trace passes at
`c/magma/trace/out/matrix_ordinary_player_absorption_candidate_1/summary.md`.
Both random controls, Resistance, unprotected cactus/fire, Fire Resistance,
Poison, Regeneration, Wither, and melee pass 11/11 at
`c/magma/trace/out/matrix_ordinary_player_absorption_affected_1/summary.md`.
The state is one fixed float and damage consumption is constant-time. Native
effect and mob suites pass. The CPU aggregate passes with a 294,844 KB peak
and zero swap at
`c/magma/trace/out/test_runtime_ordinary_absorption_final.log`. GPU timing and
promotion remain deferred while GPU 1 is shared.

MobEffects.SLOWNESS 2 already used the exact 1.11.2 negative operation-2
constant, but lacked its own strict end-to-end proof. A duration-6 Slowness II
fixture now matches all five active potion rows, travels measurably slower than
its post-expiry baseline, and restores ordinary speed exactly at
`c/magma/trace/out/matrix_ordinary_player_slowness_candidate_1/summary.md`.
Both random controls, Speed, Haste, Mining Fatigue, and Slowness pass 6/6 at
`c/magma/trace/out/matrix_ordinary_player_slowness_affected_1/summary.md`.
This is a test-only coverage closure, not a claimed old-C fix, and adds no
runtime work. It remains in the current full-source performance-pending batch
while GPU 1 is shared.

The authoritative state schema now exposes `max_health` and `absorption`
directly in both Java and magma. The Absorption cactus discriminator therefore
proves its gold-heart values `4,3,0` rather than relying only on unchanged
health; the direct-attribute rerun passes at
`c/magma/trace/out/matrix_ordinary_player_absorption_attributes_candidate_1/summary.md`.

MobEffects.HEALTH_BOOST 21 now adds four maximum-health points per amplifier
level through a cached attribute refreshed only when the bounded effect list
changes. A duration-3 Health Boost II fixture gives Java maximum health 28 and
food timer 1,2 while health remains 20, then restores maximum health 20 and
timer zero on expiry. Old magma holds maximum health 20 and timer zero at
`c/magma/trace/out/matrix_ordinary_player_health_boost_probe_1/summary.md`.
The corrected direct attribute, timer, health, absorption, potion, and raw-
world trace passes at
`c/magma/trace/out/matrix_ordinary_player_health_boost_candidate_2/summary.md`.
Both random controls plus Jump Boost, Fire Resistance, Resistance, Absorption,
Hunger, Poison, Regeneration, and Wither pass 10/10 at
`c/magma/trace/out/matrix_ordinary_player_health_boost_affected_1/summary.md`.
The shared vitals maximum is now a runtime value, so natural regeneration,
healing, damage clamping, and Regeneration consume the same cap. Native effect
coverage passes, and the scalar vitals output remains identical to its 400-line
Java golden. GPU timing and performance promotion remain deferred while GPU 1
is shared.

MobEffects.REGENERATION 10 now uses vanilla's `50 >> amplifier` readiness
cadence and heals one point when below maximum health. The focused fixture
starts Regeneration I at duration 52 and the burn counter at 42, causing a
scheduled damage point and duration-50 heal in the same tick. Old magma keeps
fire, hurt state, potion duration, blocks, and light exact but remains at
health 19 from tick 2 at
`c/magma/trace/out/matrix_ordinary_player_regeneration_probe_1/summary.md`.
The corrected five-tick trace stays at health 20 while retaining hurt time
9, 8, 7 at
`c/magma/trace/out/matrix_ordinary_player_regeneration_candidate_1/summary.md`.
Both random controls, drowning, the fire family, Fire Resistance, Hunger, and
Poison pass 9/9 at
`c/magma/trace/out/matrix_ordinary_player_regeneration_affected_1/summary.md`.
The action adds no scan and the native effect gate covers the same-tick
recovery.

MobEffects.WITHER 20 now uses vanilla's `40 >> amplifier` readiness cadence for
effects in the ordinary active-potion list. Old magma ages the Wither II
duration exactly but misses the duration-20 armor-bypassing hit, first
diverging in health and hurt state at tick 0 at
`c/magma/trace/out/matrix_ordinary_player_wither_probe_1/summary.md`. The
corrected four-tick trace remains at health 19, ages hurt time 10 through 7,
and ages duration 19 through 16 at
`c/magma/trace/out/matrix_ordinary_player_wither_candidate_1/summary.md`.
The affected random, drowning, melee, fire, and potion family passes 10/10 at
`c/magma/trace/out/matrix_ordinary_player_wither_affected_1/summary.md`. The
combined Regeneration and Wither aggregate passes with a 295,708 KB peak and
zero swap at
`c/magma/trace/out/test_runtime_ordinary_regeneration_wither_final.log`.
Its shared-host wall time is excluded, and GPU performance promotion remains
pending while GPU 1 is shared.

MobEffects.STRENGTH 5 and WEAKNESS 18 now contribute their amplifier-scaled
`+3` and `-4` modifiers to the ordinary player attack-damage attribute. The
focused Strength I fixture uses a ten-health pig and the established physical
click tape. Old magma deals the basic one-point empty-hand hit at tick 4 at
`c/magma/trace/out/matrix_ordinary_player_strength_probe_1/summary.md`; the
corrected four-point hit, hurt immunity, rejected tick-6 follow-up, exhaustion,
and duration trace pass at
`c/magma/trace/out/matrix_ordinary_player_strength_candidate_1/summary.md`.
The affected family passes 8/8 at
`c/magma/trace/out/matrix_ordinary_player_strength_affected_1/summary.md`.

Weakness I pushes the empty-hand attribute below its zero floor. After the
shared modifier arithmetic, the deliberate old row has the correct unchanged
health but incorrectly creates hurt state and exhaustion at
`c/magma/trace/out/matrix_ordinary_player_weakness_probe_1/summary.md`. The
corrected path treats zero damage as a targeted rejection while still resetting
swing cooldown, passing strict at
`c/magma/trace/out/matrix_ordinary_player_weakness_candidate_1/summary.md`.
Strength, Weakness, baseline melee, both random controls, and neighboring
potions pass 8/8 at
`c/magma/trace/out/matrix_ordinary_player_weakness_affected_1/summary.md`.
The potion scan occurs only when consuming a queued attack. The combined
aggregate passes with a 296,176 KB peak and zero swap at
`c/magma/trace/out/test_runtime_ordinary_strength_weakness_final.log`.
Its shared-host wall time is excluded, and GPU performance promotion remains
pending while GPU 1 is shared.

Physical right-click activation now reaches the represented redstone controls.
The old integrated-server dispatch admitted containers but excluded lever 69,
stone button 77, and wooden button 143. Java therefore toggled or pressed the
target while magma left it unchanged. The corrected wall fixtures match two
lever toggles, stone +20 release, wooden +30 release, the lamp's +4 handoff,
click cooldown, queue state, raw blocks, and light 3/3 at
`c/magma/trace/out/matrix_redstone_player_control_use_candidate_2/summary.md`.
The affected family passes 9/9 at
`c/magma/trace/out/matrix_redstone_player_control_use_affected_1/summary.md`.
All work is packet- and click-driven; the idle tick receives no scan.

The same physical path now admits repeaters 93/94 and comparators 149/150.
The deliberate old-C probe isolates one mismatch per case: Java changes
repeater metadata 2 to 6 or comparator metadata 2 to 6 and resets click
cooldown, while old magma does neither at
`c/magma/trace/out/matrix_redstone_player_diode_use_probe_1/summary.md`. The
corrected explicit behavior gates pass 2/2 with all 26 simulated state
features, raw blocks, light, and zero unintended scheduled work at
`c/magma/trace/out/matrix_redstone_player_diode_use_candidate_2/summary.md`.
Repeater clicks cycle the four metadata delays and perform the vanilla flag-3
neighbor/observer notification. Comparator clicks toggle mode, update
observers, and immediately recompute the existing bounded comparator tile.
Both random controls, four-delay/off/lock repeater paths, and comparator
compare/subtract/priority paths pass 9/9 at
`c/magma/trace/out/matrix_redstone_player_diode_use_affected_1/summary.md`.

Physical wooden access blocks now use the same integrated-server boundary.
An upper-half door click previously reached the one-cell client state machine,
which deliberately could not resolve the paired lower half, and produced no
mutation. Oak gate and trapdoor clicks mutated the client window exactly but
missed the server success swing. The probe isolates the door block mismatch
and one cooldown mismatch in every row at
`c/magma/trace/out/matrix_ordinary_player_wooden_access_use_probe_1/summary.md`.
The corrected upper oak door, opposite-facing oak gate, and bottom oak
trapdoor pass 3/3 with exact paired metadata, facing flip, cooldown, queue,
raw blocks, and light at
`c/magma/trace/out/matrix_ordinary_player_wooden_access_use_candidate_1/summary.md`.
The fixed dispatch covers all six wooden door and fence-gate registries;
focused native loops exhaust them and preserve iron door/trapdoor refusal.

Live redstone power now opens and closes all door, fence-gate, and trapdoor
registries in the represented world. The deliberate source-placement probes
keep all 26 simulated features exact while old magma changes only the source;
Java additionally changes oak door lower/upper metadata `1/8` to `5/10`, gate
`0` to `12`, and trapdoor `0` to `4` at
`c/magma/trace/out/matrix_redstone_power_wooden_access_probe_1/summary.md`.
The corrected three rows pass state, behavior, raw blocks, and light at
`c/magma/trace/out/matrix_redstone_power_wooden_access_candidate_1/summary.md`.
The update is neighbor-driven with no idle scan. Native tests exhaust seven
door IDs, six gate IDs, and both trapdoors through power-on and power-off.
Redstone-block removal closes the same three oak fixtures exactly at
`c/magma/trace/out/matrix_redstone_unpower_wooden_access_candidate_1/summary.md`.
Both random controls, all three physical-use rows, ordinary open/closed access
collision, and the piston door-pair guard pass 10/10 at
`c/magma/trace/out/matrix_redstone_power_wooden_access_affected_1/summary.md`.

Physical daylight-detector inversion is now represented. Old magma misses the
normal and inverted click completely, leaving one raw block mismatch and the
success-cooldown divergence in each row at
`c/magma/trace/out/matrix_redstone_player_daylight_detector_use_probe_1/summary.md`.
The fixed clear-weather path uses exact saved skylight, world time, vanilla's
celestial-angle easing and sin-table cosine, then swaps `151:15` to `178:0` or
`178:0` to `151:15` at noon. Both rows pass state, behavior, blocks, block
light, and skylight at
`c/magma/trace/out/matrix_redstone_player_daylight_detector_use_candidate_2/summary.md`.
Detector metadata is also a weak-power source for the represented dust,
diode, comparator, piston, lamp, and access-block queries. Ten neighboring
random, interaction, collision, and piston rows pass at
`c/magma/trace/out/matrix_redstone_player_daylight_detector_use_affected_1/summary.md`.
The full native aggregate passes with a 294,856 KB peak and zero swap at
`c/magma/trace/out/test_runtime_redstone_daylight_detector_use_final.log`.
Periodic detector tile ticks are now represented by an allocation-free,
fixed-capacity active list. Old magma leaves a restored noon `151:0` detector
stale at
`c/magma/trace/out/matrix_redstone_daylight_detector_periodic_probe_1/summary.md`;
the exact total-time-160 update passes at
`c/magma/trace/out/matrix_redstone_daylight_detector_periodic_candidate_1/summary.md`.
Normal lamp-on and inverted lamp-off circuits pass raw blocks, block light,
and skylight at
`c/magma/trace/out/matrix_redstone_daylight_detector_periodic_lamp_candidate_2/summary.md`.
That proof found and corrected detector IDs 151/178 in the zero-opacity light
table. The neighboring family is exact across the 13-case affected run and
its one stable reporting rerun at
`c/magma/trace/out/matrix_redstone_daylight_detector_periodic_light_affected_1/summary.md`
and
`c/magma/trace/out/matrix_redstone_daylight_detector_periodic_light_affected_rerun_1/summary.md`.
Weather-strength attenuation remains separate F-01 work.

Physical cake eating is now represented. A locked food-18 fixture isolates
old magma's missing first bite: Java changes `92:0` to `92:1`, food 18 to 20,
saturation 5.0 to 5.4, and the successful-use cooldown while all other
observed state remains exact at
`c/magma/trace/out/matrix_ordinary_player_use_cake_probe_2/summary.md`.
The corrected case passes at
`c/magma/trace/out/matrix_ordinary_player_use_cake_candidate_1/summary.md`.
A physical seven-click case begins at food 6, follows the cake's receding west
edge with one measured 15-degree turn, and proves every serving through exact
block removal at
`c/magma/trace/out/matrix_ordinary_player_eat_whole_cake_candidate_3/summary.md`.
The 9/9 affected family covers random controls, comparator output, collision,
piston destruction, pig pushing, and comparator power-off at
`c/magma/trace/out/matrix_ordinary_player_use_cake_affected_1/summary.md`.
The exact-source aggregate through cake passes in 5:50 with a 296,124 KB peak
and zero swap at
`c/magma/trace/out/test_runtime_daylight_periodic_cake_final.log`.

Physical flower-pot insertion is now represented. The valid 60-degree
old-magma probe shows Java consume held red flower `38:2` on the client tick,
commit it to the pot tile on the next server tick, and perform the late success
swing; old magma instead places a stray flower block and leaves the tile empty
at
`c/magma/trace/out/matrix_ordinary_player_pot_red_flower_probe_2/summary.md`.
The corrected row is exact at
`c/magma/trace/out/matrix_ordinary_player_pot_red_flower_candidate_3/summary.md`.
Native tests cover all 21 canonical sapling/fern/dead-bush/flower/mushroom/
cactus item-meta pairs and occupied/invalid refusal. Both random controls,
ordinary collision, piston side/above entity collision, and occupied-pot
destruction pass 7/7 at
`c/magma/trace/out/matrix_ordinary_player_pot_red_flower_affected_1/summary.md`.
The exact-source aggregate through flower pots passes in 5:41 with a 296,388
KB peak and zero swap at
`c/magma/trace/out/test_runtime_daylight_periodic_cake_flower_pot_final.log`.

Physical record insertion into an empty jukebox is now represented. Old magma
leaves the record held, tile empty, metadata zero, and cooldown untouched at
`c/magma/trace/out/matrix_ordinary_player_insert_record_13_probe_1/summary.md`.
The corrected record-13 row installs tile item `2256:0`, changes jukebox
metadata `84:0` to `84:1`, consumes the held item, and matches the exact late
swing at
`c/magma/trace/out/matrix_ordinary_player_insert_record_13_candidate_2/summary.md`.
Held and full inventory are now captured from the locked server player rather
than packet-latent client state. Native tests exhaust record IDs 2256..2267;
random controls, cake/flower interactions, and saved jukebox comparator
strengths 1/12 pass 7/7 at
`c/magma/trace/out/matrix_ordinary_player_insert_record_13_affected_2/summary.md`.
Physical record ejection is now represented for record 13 and record wait.
Both clear metadata and the tile slot, reset the ordinary click cooldown, and
spawn the exact record EntityItem. The forced-overlap row also matches
`Entity.pushOutOfBlocks` position, velocity, yaw, age, pickup delay, and entity
ID after pinning Java's otherwise clock-seeded constructor RNG at the saved
input boundary. The two focused rows and five neighboring controls pass 7/7
at
`c/magma/trace/out/matrix_ordinary_player_eject_record_affected_1/summary.md`.
Native coverage inserts and ejects all 12 records and proves full-item-pool
rejection is atomic across tile state, RNG, and entity ID. A locked Java
fixture proves insertion/ejection emits `(1010,item)` then `(1010,0)` for every
record. Native world and sound rings match those pairs, and interactive audio
starts/stops the corresponding bounded OGG stream at the exact block position.
Firework blast/twinkle audio is separately promoted in A-01. Broader music,
ambient, category, and output-comparison work remains A-01. The final
exact-source aggregate passes in 5:15.70 with a 296,108 KB peak and zero swap
at
`c/magma/trace/out/test_runtime_daylight_periodic_cake_flower_pot_jukebox_ejection_final.log`.

Redstone-powered TNT now covers both vanilla ignition entry points and every
tick before detonation. The neighbor-change probe records old magma retaining
block 46 and creating no entity while Java replaces it with one
`EntityTNTPrimed` at
`c/magma/trace/out/matrix_redstone_tnt_ignite_probe_1/summary.md`. The direct
powered-placement probe separately records the missing `BlockTNT.onBlockAdded`
path at
`c/magma/trace/out/matrix_redstone_tnt_direct_add_probe_1/summary.md`. The
corrected paths match block removal, fixed-set entity allocation, entity ID,
constructor `Math.random`, initial motion, fuse 80, and the complete 79-tick
gravity/collision/drag/bounce trajectory through fuse 1. All three TNT rows and
five neighboring controls pass at
`c/magma/trace/out/matrix_redstone_tnt_affected_2/summary.md`; the native
aggregate passes in 5:16.39 with a 295,856 KB peak and zero swap at
`c/magma/trace/out/test_runtime_redstone_tnt_ignition_fuse_final.log`.
Fuse-zero handling now has the strict isolated-crater, one-hit chain, and
open-air unarmored-player response slices described below. General detonation
still lacks block drops, fire, the complete resistance table, non-collidable
ray targets and broader shaped occluders, armor/resistance variants, non-player
entity response, multiple-hit TNT ordering, emitted sound, particles, and
rendering. Prime sound, rendering, and capsule restore also remain open under
A-01, V-01, and O-02 respectively.

Player ignition of TNT is represented for both flint and steel 259 and fire
charge 385. The deliberate old-product flint probe keeps TNT, places adjacent
fire, and damages the tool one tick before Java's server activation at
`c/magma/trace/out/matrix_ordinary_player_ignite_tnt_flint_probe_1/summary.md`.
The corrected physical-use packet boundary removes TNT, applies one tool damage
or consumes one charge, and creates the exact fuse-80 entity whose same-tick
update reaches fuse 79. The two strict item rows pass together at
`c/magma/trace/out/matrix_ordinary_player_ignite_tnt_items_candidate_1/summary.md`.
The fire-charge row also matches `EntityPlayer.onUpdate` resetting attack
cooldown when the last charge empties the main hand. Both items plus eight
neighboring TNT, lamp, piston, door, wire, and jukebox cases pass 10/10 at
`c/magma/trace/out/matrix_ordinary_player_ignite_tnt_items_affected_1/summary.md`.
Native tests preserve invalid-item refusal and make fixed-pool exhaustion
atomic across block, inventory, entity ID, and RNG. The aggregate passes under
concurrent CPU validation in 5:39.24 with a 298,736 KB peak and zero swap at
`c/magma/trace/out/test_runtime_tnt_player_ignition_final.log`. Burning-arrow
collision ignition is now resolved. The old-C proof retains block 46 while
Java creates primed TNT at
`c/magma/trace/out/matrix_redstone_tnt_burning_arrow_probe_1/summary.md`.
The corrected positive and expired-fire negative repeat exactly 3/3 each at
`c/magma/trace/out/matrix_redstone_tnt_burning_arrow_repeat_3/summary.md`, and
the affected TNT/redstone family passes 12/12 at
`c/magma/trace/out/matrix_redstone_tnt_burning_arrow_affected_2/summary.md`.
Magma decrements the controlled arrow's fire before collision, requires the
remaining value to be positive, removes TNT, constructs the exact primed
entity, and advances it to fuse 79 in the same tick. The Java harness restores
the saved causal cursors at the armed TNT contact so unrelated world-generation
animals earlier in the same server tick cannot contaminate constructor ID or
motion. This instrumentation is dormant outside that explicit oracle fixture.
The exact-source aggregate passes under unrelated host training load in
5:40.55 with a 303,104 KB peak and zero swap at
`c/magma/trace/out/test_runtime_tnt_burning_arrow_final.log`. The bounded
fuse-zero crater and explosion-triggered short-fuse slices follow below.

Fuse-zero TNT now has one strict no-drop crater slice. The clean old-C probe
shows exact player/entity state but zero block mutations against Java's six
glass removals at
`c/magma/trace/out/matrix_tnt_fuse_zero_glass_probe_2/summary.md`. The live
candidate consumes the exact 1,352 `World.rand` face-ray draws plus the two
`doExplosionB` sound-pitch draws, uses the promoted stone resistance for the
staged floor, retires the saved fuse-1 entity, and removes all six glass blocks. It repeats 3/3 at
`c/magma/trace/out/matrix_tnt_fuse_zero_glass_repeat_1/summary.md`, and the
affected TNT/redstone family passes 13/13 at
`c/magma/trace/out/matrix_tnt_fuse_zero_glass_affected_1/summary.md`. The
detonation cursor pin is armed only for the explicit oracle fixture. General
explosion parity remains open for drops, fire, the full resistance table,
non-collidable ray targets and broader shaped occluders, armor/resistance
variants, non-player entity response, emitted sound, particles, and rendering.
The exact-source aggregate passes under
shared-host load in 5:35.70 with a 299,292 KB peak and zero swap at
`c/magma/trace/out/test_runtime_tnt_fuse_zero_glass_final.log`.

Explosion-triggered TNT now has one strict short-fuse chain slice. The clean
old-C probe removes the affected blocks but creates no chained entity at
`c/magma/trace/out/matrix_tnt_explosion_chain_prime_probe_1/summary.md`. The
corrected path removes five glass cells and one TNT, allocates the saved next
entity ID, consumes two constructor `Math.random` draws, samples the shortened
fuse with the exact post-sound `World.rand.nextInt(20)`, and ticks fuse 10 to 9
on the same boundary. The checker derives those cursor and fuse values from the
saved pre-detonation state. Three independent captures pass 3/3 at
`c/magma/trace/out/matrix_tnt_explosion_chain_prime_repeat_1/summary.md`; the
affected TNT/redstone family passes 14/14 at
`c/magma/trace/out/matrix_tnt_explosion_chain_prime_affected_1/summary.md`.
The exact-source aggregate passes in 5:16.98 with a 303,588 KB peak and zero
swap at
`c/magma/trace/out/test_runtime_tnt_explosion_chain_prime_final_2.log`.
Multiple hit TNTs and Java affected-block iteration ordering remain open, as do
drops, fire, full resistance coverage, non-collidable ray targets and broader
shaped occluders, armor/resistance variants, non-player entity response,
emitted sound, particles, and rendering.

Open-air TNT damage and packet knockback now have one strict unarmored-player
slice. The deliberate old-product probe applies seven damage from its prior
eye-centered approximation but creates no hurt state or knockback, while Java
applies three damage, adds 0.1 exhaustion, and emits the tracked velocity
response at
`c/magma/trace/out/matrix_tnt_explosion_player_open_air_probe_2/summary.md`.
The corrected event boundary uses player feet for damage range, eye height for
the impulse direction, and measured density one. It reproduces the following
self-tracking velocity packet, 1/8000 motion truncation, client ground friction,
hurt aging, and FoodStats update. The strict candidate passes at
`c/magma/trace/out/matrix_tnt_explosion_player_open_air_candidate_3/summary.md`,
three independent captures pass 3/3 at
`c/magma/trace/out/matrix_tnt_explosion_player_open_air_repeat_1/summary.md`,
and the affected TNT/redstone family passes 15/15 at
`c/magma/trace/out/matrix_tnt_explosion_player_open_air_affected_1/summary.md`.
The final CPU aggregate passes in 5:13.29 with a 301,368 KB peak and zero swap
at
`c/magma/trace/out/test_runtime_tnt_explosion_player_open_air_final.log`.
Full-cube obstructed player exposure is now represented by one bounded
diagnostic. A glass occluder blocks 21 of the exact 45 standing-player rays,
giving density 0.53333336, damage 4, packet X -0.09516628, and authoritative
server X velocity -0.051960797397907814. The old product instead uses density
one, over-damages to health 13, and over-displaces to X 8.321625 at
`c/magma/trace/out/matrix_tnt_explosion_player_obstructed_probe_4/summary.md`.
Three corrected captures pass at
`c/magma/trace/out/matrix_tnt_explosion_player_obstructed_candidate_2/summary.md`,
and the affected TNT/redstone family passes 16/16 at
`c/magma/trace/out/matrix_tnt_explosion_player_obstructed_affected_1/summary.md`.
The behavior gate accepts either applied or one-observation-deferred Java
client packet state because both were measured across independent captures;
the detonation density/damage/packet, authoritative Java and C server motion,
C client response, causal cursor, and raw block transition remain exact. A
focused native regression pins the seed-dependent crater and fails within 24
seconds; the full aggregate passes in 6:05.53 with a 307,616 KB peak and zero
swap at
`c/magma/trace/out/test_runtime_tnt_explosion_player_obstructed_final_3.log`.
Open-air Resistance I, a plain diamond chestplate, Blast Protection IV, and
their combined armor-then-Resistance-then-enchantment order are now exact
bounded diagnostics. Durability and enchant payload survive the hit exactly;
Java's floored sub-one Blast Protection knockback term is correctly zero in
this fixture. Evidence is
`c/magma/trace/out/matrix_tnt_explosion_player_defense_regression_1/summary.md`
and `c/magma/trace/out/test_runtime_tnt_explosion_player_defense_final.log`.
One locked-pig open-air slice now matches Java exactly for AABB exposure,
damage 9, health 1, motion, and the fresh hurt/invulnerability timers at
`c/magma/trace/out/matrix_tnt_explosion_mob_candidate_2/summary.md`; the
nine-case TNT living regression passes at
`c/magma/trace/out/matrix_tnt_explosion_living_regression_1/summary.md`.
The full CPU aggregate passes in 5:55.93 with a 308,092 KB peak and zero swap
at `c/magma/trace/out/test_runtime_tnt_explosion_mob_final.log`.
The reversed saved order is also strict: TNT hits first, then the NoAI pig
ages hurt 10 to 9 and invulnerability 20 to 19 while damping the new impulse
by 0.98. Both order rows pass at
`c/magma/trace/out/matrix_tnt_explosion_mob_order_final_1/summary.md`, and the
complete ten-case family passes at
`c/magma/trace/out/matrix_tnt_explosion_ordered_living_regression_1/summary.md`.
The exact-source CPU aggregate passes in 6:05.70 with a 308,416 KB peak and
zero swap at
`c/magma/trace/out/test_runtime_tnt_explosion_mob_order_final.log`.
Lethal no-loot response is exact in both orders too: pig-first exposes death
time 0 and retires at tick 20, while TNT-first exposes death time 1 and retires
at tick 19. All four living/order rows pass at
`c/magma/trace/out/matrix_tnt_explosion_mob_living_order_final_1/summary.md`.
Explosion density now also honors outline-only blocks whose entity collision
box is null. A standing torch masks exactly one third of the locked pig's
rays, changing damage 9 to 6 and health 1 to 4; the torch mutation, support,
block light, motion, and timers are exact. The old-path negative, three strict
repeats, 13-case affected family, and CPU aggregate are retained at
`c/magma/trace/out/matrix_tnt_explosion_mob_torch_occluded_probe_2/summary.md`,
`c/magma/trace/out/matrix_tnt_explosion_mob_torch_occluded_repeat_1/summary.md`,
`c/magma/trace/out/matrix_tnt_explosion_outline_regression_1/summary.md`, and
`c/magma/trace/out/test_runtime_tnt_explosion_outline_final.log`.
A surviving dropped stone item is now strict too. The old path retains health
5 and zero motion; the corrected item-before-TNT boundary advances age once,
then applies damage 4 and the exact undamped X/Y impulse while retaining its
position. The negative, three repeats, expanded 14-case TNT family, and full
CPU aggregate are retained at
`c/magma/trace/out/matrix_tnt_explosion_item_surviving_probe_1/summary.md`,
`c/magma/trace/out/matrix_tnt_explosion_item_surviving_repeat_1/summary.md`,
`c/magma/trace/out/matrix_tnt_explosion_item_regression_1/summary.md`, and
`c/magma/trace/out/test_runtime_tnt_explosion_item_final.log`.
The reverse loaded order is strict as well: TNT applies the hit first, then the
item moves by the new impulse and damps it by float 0.98 in its same-tick
update. The negative, three repeats, plate/piston affected controls, and final
CPU aggregate are retained at
`c/magma/trace/out/matrix_tnt_explosion_item_tnt_first_probe_1/summary.md`,
`c/magma/trace/out/matrix_tnt_explosion_item_tnt_first_repeat_1/summary.md`,
`c/magma/trace/out/matrix_tnt_explosion_item_order_affected_3/summary.md`, and
`c/magma/trace/out/test_runtime_tnt_explosion_item_order_final.log`.
Ordinary lethal and Nether Star protection branches are strict too. A stone
item at six blocks retires on the blast tick; a Nether Star at seven blocks
retains health 5 while receiving the exact impulse. Both explicit gates pass
and repeat 3/3 at
`c/magma/trace/out/matrix_tnt_explosion_item_lifecycle_candidate_1/summary.md`
and
`c/magma/trace/out/matrix_tnt_explosion_item_lifecycle_repeat_1/summary.md`;
focused native exception coverage passes at
`c/magma/trace/out/test_tnt_explosion_item_lifecycle_final.log`.
The next represented non-living category is strict for TNT-first boats. The
old path leaves the gravity-free boat parked; the corrected path applies
damage-taken 40, preserves the boat at Java's strict greater-than-40 break
threshold, applies the exact impulse, and runs float-0.9 motion before moving
on the same tick. The negative, three repeats, seven affected entity-order and
redstone controls, focused native proof, and full CPU aggregate are retained at
`c/magma/trace/out/matrix_tnt_explosion_boat_probe_1/summary.md`,
`c/magma/trace/out/matrix_tnt_explosion_boat_repeat_1/summary.md`,
`c/magma/trace/out/matrix_tnt_explosion_boat_affected_1/summary.md`,
`c/magma/trace/out/test_tnt_explosion_boat_final.log`, and
`c/magma/trace/out/test_runtime_tnt_explosion_boat_final.log`.
Arrow-first TNT response is strict too. The arrow updates before TNT, retains
position and zero rotation, then receives the exact raw X/Y blast motion. The
old-path negative, three repeats, nine affected projectile/redstone/entity
controls, focused native proof, and full CPU aggregate are retained at
`c/magma/trace/out/matrix_tnt_explosion_arrow_probe_1/summary.md`,
`c/magma/trace/out/matrix_tnt_explosion_arrow_repeat_1/summary.md`,
`c/magma/trace/out/matrix_tnt_explosion_arrow_affected_1/summary.md`,
`c/magma/trace/out/test_tnt_explosion_arrow_final.log`, and
`c/magma/trace/out/test_runtime_tnt_explosion_arrow_final.log`.
XP-orb-first TNT response is strict too. The orb completes its ordinary
attraction/gravity/move/drag update before TNT, then receives damage 4 and the
exact raw X/Y blast motion while exposing private health 1 and both color
counters. The old-path negative, three repeats, nine affected XP/redstone and
entity controls, focused native proof, and full CPU aggregate are retained at
`c/magma/trace/out/matrix_tnt_explosion_xp_probe_1/summary.md`,
`c/magma/trace/out/matrix_tnt_explosion_xp_repeat_1/summary.md`,
`c/magma/trace/out/matrix_tnt_explosion_xp_affected_1/summary.md`,
`c/magma/trace/out/test_tnt_explosion_xp_final.log`, and
`c/magma/trace/out/test_runtime_tnt_explosion_xp_final.log`.
TNT-first falling sand is strict too. A scheduled sand entity is already
loaded before fuse-three TNT's final update; TNT has the lower ID, applies the
exact raw impulse, then the sand's same-tick update applies gravity, movement,
and float-0.98 drag. The old path, three strict repeats, ten affected falling/
TNT/entity controls, focused native proof, and full CPU aggregate are retained
at
`c/magma/trace/out/matrix_tnt_explosion_falling_sand_probe_3/summary.md`,
`c/magma/trace/out/matrix_tnt_explosion_falling_sand_repeat_1/summary.md`,
`c/magma/trace/out/matrix_tnt_explosion_falling_sand_affected_1/summary.md`,
`c/magma/trace/out/test_tnt_explosion_falling_sand_final.log`, and
`c/magma/trace/out/test_runtime_tnt_explosion_falling_sand_final.log`.
TNT-first small-fireball response is strict too. Fuse-one TNT has the lower
saved ID, applies the exact raw impulse, and then the fireball's same-tick
update moves, rotates with vanilla's table-based `MathHelper.atan2`, and damps
by float 0.95. The old path, strict gate, three repeats, eleven affected
TNT/projectile controls, ordinary eight-tick trajectory, focused native proof,
and full CPU aggregate are retained at
`c/magma/trace/out/matrix_tnt_explosion_small_fireball_probe_1/summary.md`,
`c/magma/trace/out/matrix_tnt_explosion_small_fireball_behavior_1/summary.md`,
`c/magma/trace/out/matrix_tnt_explosion_small_fireball_repeat_1/summary.md`,
`c/magma/trace/out/matrix_tnt_explosion_small_fireball_affected_1/summary.md`,
`c/magma/trace/out/small_fireball_trajectory_tnt_final`,
`c/magma/trace/out/test_tnt_explosion_small_fireball_final.log`, and
`c/magma/trace/out/test_runtime_tnt_explosion_small_fireball_final.log`.
Already-primed TNT response is strict in both loaded orders. Source-first
applies the blast before the target's gravity/move/float-0.98 update;
target-first applies the raw impulse after that update. The source-first old
path, both three-repeat gates, 12-case affected family, combined order gate,
focused native proof, and full CPU aggregate are retained at
`c/magma/trace/out/matrix_tnt_explosion_primed_tnt_probe_1/summary.md`,
`c/magma/trace/out/matrix_tnt_explosion_primed_tnt_repeat_1/summary.md`,
`c/magma/trace/out/matrix_tnt_explosion_primed_tnt_target_first_repeat_1/summary.md`,
`c/magma/trace/out/matrix_tnt_explosion_primed_tnt_affected_2/summary.md`,
`c/magma/trace/out/matrix_tnt_explosion_primed_tnt_orders_final_2/summary.md`,
`c/magma/trace/out/test_tnt_explosion_primed_tnt_orders_final.log`, and
`c/magma/trace/out/test_runtime_tnt_explosion_primed_tnt_final.log`.
Standalone End-crystal ticking and TNT-triggered destruction are strict in the
Overworld. The saved fixture carries the authoritative entity ID,
`innerRotation`, and bottom flag; its ordinary update advances rotation 0 to
1. A lower-ID fuse-one TNT then destroys the paired crystal before its update,
and the crystal synchronously emits its vanilla size-six smoking explosion.
Both entities retire, the high open-air volume has zero mutations, the distant
player is unchanged, and Java/magma finish at the same `World.rand` cursor
after exactly 2,708 advances. The deliberate disabled-response probe, three
strict repeats, standalone control, 14-case affected family, focused native
proof, full CPU aggregate, and scalar performance guard are retained at
`c/magma/trace/out/matrix_tnt_explosion_end_crystal_probe_1/summary.md`,
`c/magma/trace/out/matrix_tnt_explosion_end_crystal_repeat_1/summary.md`,
`c/magma/trace/out/matrix_end_crystal_idle_candidate_1/summary.md`,
`c/magma/trace/out/matrix_tnt_explosion_end_crystal_affected_final_1/summary.md`,
`c/magma/trace/out/test_tnt_explosion_end_crystal_final.log`,
`c/magma/trace/out/test_tnt_explosion_end_crystal_candidate.log`, and
`c/magma/trace/out/perf_guard_tnt_end_crystal_cpu_1.json`.
End-dimension standalone crystals now also reproduce their vanilla fire
maintenance. The deliberate disabled-write proof leaves one Java fire cell
missing at
`c/magma/trace/out/matrix_end_crystal_fire_probe_2/summary.md`; the corrected
row repeats 3/3 and passes with its Overworld idle and TNT-response controls at
`c/magma/trace/out/matrix_end_crystal_fire_repeat_1/summary.md` and
`c/magma/trace/out/matrix_end_crystal_fire_affected_final_1/summary.md`.
Raw block state is exact. Ambient End block light beyond the captured capsule
is not compared; the diagnostic candidate had no light mismatch within 15
blocks of the controlled fire. Focused native coverage passes at
`c/magma/trace/out/test_end_crystal_fire_final.log`, and the full CPU aggregate
passes in 5:42.21 with a 306,804 KB peak and zero swap at
`c/magma/trace/out/test_runtime_end_crystal_fire_final.log`.
Saved beam-target state is also exact. The old product loses only the target
presence field at
`c/magma/trace/out/matrix_end_crystal_beam_probe_1/summary.md`; the corrected
row repeats 3/3 and all four adjacent crystal controls pass at
`c/magma/trace/out/matrix_end_crystal_beam_repeat_1/summary.md` and
`c/magma/trace/out/matrix_end_crystal_beam_affected_1/summary.md`. The fixed
runtime pool now exposes each saved crystal to the live render-view stream
with exact rotation, bottom flag, and target coordinates. Native coverage is
at `c/magma/trace/out/test_end_crystal_beam_candidate.log`; the full CPU
aggregate passes in 6:04.60 with a 306,856 KB peak and zero swap at
`c/magma/trace/out/test_runtime_end_crystal_beam_final.log`. The CPU-only guard
passes at 4,195 scalar steps/s at
`c/magma/trace/out/perf_guard_end_crystal_beam_cpu_1.json`.
The corresponding render path is now present too. It uses the exact jar
`endercrystal_beam.png`, a standalone repeating sampler, the vanilla tapered
eight-sided smooth-shaded strip and bob/rotation transform, disabled standard
item lighting while retaining the owning entity's lightmap, and two-sided
coverage equivalent to vanilla's disabled culling. On the real-Java
scenario tape's beam-only sky strip at tick 14, differing pixels fall from
1,799 to 284; the full frame improves from 9.70 to 8.91 mean/channel. The
remaining full-scene gate failure is not attributed to the beam: the same tape
contains already-known terrain/fog, HUD, crystal-base, and slime residuals.
Exact texture and numerical geometry tests pass, the four affected state rows
pass at
`c/magma/trace/out/matrix_end_crystal_beam_geometry_affected_1/summary.md`,
and the full CPU aggregate passes at
`c/magma/trace/out/test_runtime_end_crystal_beam_geometry_final.log`. The
284-pixel beam-region residual remains in V-01 rather than being
called exact.
Dragon-fight crystal notification is now wired for the represented live arena.
The real-Java bounded probe and shared C state machine agree on healing damage,
non-healing control, player-attributed holding-to-strafe, and the no-nearby-
player negative. Product melee, arrow, small-fireball, and recursive arena
explosion paths preserve Java's mark-dead, nested-explosion, then notification
order. The four adjacent standalone-crystal controls pass at
`c/magma/trace/out/matrix_dragon_crystal_notify_affected_1/summary.md`; focused,
full CPU, and performance evidence is at
`c/magma/trace/out/test_dragon_live_crystal_notify.log`,
`c/magma/trace/out/test_tnt_explosion_dragon_notify.log`,
`c/magma/trace/out/test_runtime_dragon_crystal_notify_final.log`, and
`c/magma/trace/out/perf_guard_dragon_crystal_notify_final_cpu_1.json`.
Live dragon healing-crystal selection and rendering are now bounded and
verified too. A locked real-Java server probe and the shared C method agree on
all nine transitions covering persistent references, tick-ten healing before
reselection, dead-reference clearing, the one-in-ten entity-RNG gate, nearest
selection, and Java's expanded dragon-box query. Evidence is at
`c/magma/trace/out/test_dragon_healer_java_c.log`. The real-Java 43-frame beam
tape is
`c/magma/raster/verify/tapes/scenario_dragon_healing_beam_20260803T173302Z.jsonl`;
its structural pixel gate passes. Against a controlled beam-off replay, the
implemented lightmapped beam improves the whole tick-10 frame from 2.72 to
2.71 mean/channel and the broader beam region from 10.64 to 10.20. The
remaining beam slice is measured as `texel-selection`, not an unclassified
shading error, and stays in V-01. Focused native tests and the deterministic
CPU dragon runner pass. The full CPU aggregate passes in 5:35.65 with a
306,600 KB peak at
`c/magma/trace/out/test_runtime_dragon_healer_1.log`; the frozen scalar guard
passes at a 4,984 steps/s median at
`c/magma/trace/out/perf_guard_dragon_healer_cpu_1.json`.
A bounded flaming-explosion branch is now live for non-Overworld bed use. The
ray kernel retains affected air independently of destroyed blocks; after
removal, the exact air/full-block/one-in-three test places fire and schedules
its first update. A parked real-Java `Explosion` oracle supplies both private
RNG cursors and agrees with C on the one-cell flaming positive and otherwise
identical non-flaming control at
`c/magma/trace/out/test_explosion_fire_java_c.log`. This also proves the source
distinction: TNT and End crystals are smoking but non-flaming, while invalid-
dimension beds are both. Focused, full CPU, and scalar evidence passes at
`c/magma/trace/out/test_bed_explosion_fire_1.log`,
`c/magma/trace/out/test_runtime_bed_explosion_fire_1.log`, and
`c/magma/trace/out/perf_guard_bed_explosion_fire_cpu_1.json`. Java's
clock-seeded `Explosion.explosionRNG` is not serialized in the world save;
exact replay injects the captured constructor cursor, while standalone magma
uses a deterministic event-local fallback. Multi-support HashSet iteration,
neutral-capsule transport for that transient cursor, and general explosion
fire remain open.
A bounded non-sand BlockFalling path is now exact for metadata-0 gravel over a
two-cell air column with stone support. The old-C probe missed the entity and
both raw mutations at
`c/magma/trace/out/matrix_falling_gravel_prefix/summary.md`; the paired final
sand/gravel run matches the real Java entity-ID boundary, exact nine-tick
trajectory, landing block identity, `+2` stability callback, 26 simulated
state features, and all 10,625 raw cells at
`c/magma/trace/out/matrix_falling_blocks_java_c_final/summary.md`. The full CPU
aggregate and scalar guard pass at
`c/magma/trace/out/test_runtime_falling_gravel_focused_1.log` and
`c/magma/trace/out/perf_guard_falling_gravel_cpu_1.json`. This does not yet
cover gravel flint/drop RNG, anvil state, unsupported landing/break behavior,
lateral motion, or dynamic fluid columns.
The same bounded lifecycle now implements Java's air/water/lava/fire
`canFallThrough` predicate. Three strict cases cover still water, static lava,
and fire as a single replaceable landing cell; together with the air controls,
all five pass exact state, entity trajectory, callback queue, raw blocks, and
raw block light at
`c/magma/trace/out/matrix_falling_passthrough_final/summary.md`. The pre-fix
capsule rejected each callback at its first C admission at
`c/magma/trace/out/matrix_falling_passthrough_prefix/summary.md`. The full CPU
aggregate passes in 6:14.21 with a 309,664 KB peak, and the frozen scalar guard
passes at 4,346 steps/s at
`c/magma/trace/out/test_runtime_falling_passthrough_1.log` and
`c/magma/trace/out/perf_guard_falling_passthrough_cpu_1.json`. Dynamic fluid
flow/columns, fire callback interaction, nonreplaceable landing/drop paths,
and their Math.random-backed item spawn remain open.
The first deterministic nonreplaceable landing is now exact too. Sand or
gravel falling onto a bottom stone slab stops at the half-block surface,
fails placement into the occupied slab cell, retires without a `+2` callback,
and creates one matching EntityItem. The parked real-Java command and full C
runtime agree on all 12 falling updates for both identities, the extra entity
ID, four-call Math.random cursor, exact item stack/motion/yaw, and first item
tick at `c/magma/trace/out/falling_drop_java_c_repeat_3.log`. The loaded-world
trace passes its behavior, sole raw source removal, unchanged slab, and all
10,625 block/light cells at
`c/magma/trace/out/matrix_falling_sand_bottom_slab_drop_candidate/summary.md`;
its entity set remains diagnostic because unrelated real-world falling blocks
consume the global Math/EID cursors after the saved boundary. The five prior
landing controls remain strict at
`c/magma/trace/out/matrix_falling_blocks_after_slab_drop/summary.md`, and the
final native aggregate passes in 6:10.03 with a 310,780 KB peak at
`c/magma/trace/out/test_runtime_falling_slab_drop_final.log`. The CPU floor
passes at 4,230 steps/s at
`c/magma/trace/out/perf_guard_falling_slab_drop_cpu_1.json`. Other partial
collision shapes, lateral motion, anvils, timeouts, and fixed-pool pressure
remain open.
The complementary top-half stone-slab landing is now strict. Sand falls for
the same exact nine-tick trajectory, stops on the slab's full-height collision
surface at y=78, places into the air cell above it, and queues then drains the
landed block's `+2` stability callback. The candidate and three independent
repeats pass with 26 matching state features, the exact source/landing pair,
and all 10,625 block and block-light cells at
`c/magma/trace/out/matrix_falling_top_slab_candidate_1/summary.md` and
`c/magma/trace/out/matrix_falling_top_slab_repeat_3/summary.md`. All six strict
air/fluid/fire/top-slab controls plus the expected diagnostic bottom-slab row
pass together at
`c/magma/trace/out/matrix_falling_top_slab_family/summary.md`. The final native
aggregate passes in 6:03.99 with a 326,712 KB peak and zero major faults at
`c/magma/trace/out/test_runtime_falling_top_slab.log`. Other partial collision
shapes, lateral motion, anvils, timeouts, and fixed-pool pressure remain open.
Grass path is the next exact partial-collision landing. Its metadata-zero
surface stops sand at y=77.9375 on falling update 10; placement into the
occupied path cell fails, so the source alone becomes air and one sand item
is created. The loaded-world behavior gate follows that item through
observations 10..12 with exact Y velocity, Y position, health, age, and pickup
delay. Both shaped-drop rows pass at
`c/magma/trace/out/matrix_falling_shaped_item_lifecycle_final/summary.md`, and
the eight-case affected falling family passes at
`c/magma/trace/out/matrix_falling_grass_path_family/summary.md`. As with the
bottom slab, ambient Java entities make the loaded-world global Math/EID
cursors diagnostic. The parked command removes that noise: Java and C agree
on 44 falling updates and four exact sand/gravel item drops across bottom slab
and grass path, including motion, yaw, four Math.random calls, and two entity
IDs, at `c/magma/trace/out/test_falling_drop_grass_path.log`. The default
`doEntityDrops=true` branch is proved; a false gamerule, malformed support
metadata, other partial shapes, lateral motion, anvils, timeouts, and pool
pressure remain open.
Soul sand adds the intervening 7/8-height failed-placement boundary. Sand is
visible through falling update 10 at y=77.93686484559572, clips to y=77.875
and drops on update 11, while the support remains 88:0 and no stability
callback is queued. The proof-fence rejection is retained at
`c/magma/trace/out/matrix_falling_soul_sand_red_1/summary.md`; the corrected
row passes at
`c/magma/trace/out/matrix_falling_soul_sand_candidate_1/summary.md` and 3/3 at
`c/magma/trace/out/matrix_falling_soul_sand_repeat_3/summary.md`. The expanded
nine-case family passes at
`c/magma/trace/out/matrix_falling_soul_sand_family/summary.md`. The parked Java
oracle now covers all three shaped supports and agrees with C on 66 falling
updates and six exact sand/gravel item drops at
`c/magma/trace/out/test_falling_drop_shaped_supports.log`.
An enchanting table adds the distinct 3/4-height boundary. The proof-fence
negative is retained at
`c/magma/trace/out/matrix_falling_enchanting_table_red_1/summary.md`. The first
green raw-block candidate then exposed a live-item defect: C treated id 116 as
a full cube after the drop and snapped the new item to y=78. The partial
surface integrator now lets an item rise from the exact y=77.75 surface, so
both engines match the tick-11 drop and item lifecycle through tick 12. The
corrected candidate and nine repeats pass at
`c/magma/trace/out/matrix_enchanting_table_hay_candidate_3/summary.md` and
`c/magma/trace/out/matrix_enchanting_table_hay_repeat_3/summary.md`; all 44
affected falling/fire controls pass at
`c/magma/trace/out/matrix_enchanting_table_hay_family/summary.md`. The parked
oracle agrees on 88 falling updates and eight exact sand/gravel drops at
`c/magma/trace/out/test_falling_drop_enchanting_table.log`. Enchanting-table
tile contents, the enchanting system itself, `doEntityDrops=false`, other
partial shapes, lateral motion, anvils, timeouts, and pool pressure remain
open.
A supported carpet adds the full-footprint 1/16-height boundary. Sand remains
active through update 12 at y=77.10776093180491, clips to y=77.0625 and drops
on update 13, and the resulting item matches Java through update 15. The
proof-admission rejection is retained at
`c/magma/trace/out/matrix_falling_carpet_red_1/summary.md`; the corrected row
passes three repeats at
`c/magma/trace/out/matrix_falling_carpet_repeat_3/summary.md`, and all five
shaped supports retain green behavior and block gates at
`c/magma/trace/out/matrix_falling_shaped_support_family/summary.md`. The
parked command now agrees on 114 updates and ten exact drops at
`c/magma/trace/out/test_falling_drop_carpet.log`. The fixture includes stone
below the carpet so the saved state is valid. The native suite passes in
7:26.14 with 348,624 KB peak RSS and zero swap at
`c/magma/trace/out/test_runtime_carpet.time`.

The shared CPU/CUDA fire-table probe now explicitly emits coal block id 173's
already-correct 5/5 row. The 39-line CPU result passes at
`c/magma/trace/out/test_world_tick_carpet_coal_cpu.log`; no coal product-table
change was required. GPU 1 stayed shared and untouched. Two scalar captures
under an unrelated 64-thread self-play job are preserved as contaminated
absolute-floor failures at
`c/magma/trace/out/perf_guard_falling_carpet_cpu_1.json` and
`c/magma/trace/out/perf_guard_falling_carpet_cpu70_diagnostic_1.json`; both
retain the exact trajectory hash, and clean performance promotion remains
pending.
Snow layers now cover their metadata-dependent collision and replaceability
semantics. The one-layer state has no collision, so sand falls to the stone
top at y=77, replaces 78:0 on update 13, creates no item, and drains its `+2`
stability callback. The eight-layer endpoint collides at y=77.875, retains
78:7, and creates the exact update-11 item. The old proof fence rejects the
callback at `c/magma/trace/out/matrix_falling_snow_red_1/summary.md`; both
endpoints and all six repeats pass at
`c/magma/trace/out/matrix_falling_snow_candidate_1/summary.md` and
`c/magma/trace/out/matrix_falling_snow_repeat_3/summary.md`. All seven shaped
supports remain green at
`c/magma/trace/out/matrix_falling_snow_shaped_family/summary.md`.

The parked oracle covers metadata 0..7 for both sand and gravel and agrees on
302 updates across 26 cases at
`c/magma/trace/out/test_falling_drop_snow_layers.log`. The native suite passes
in 5:46.00 at 350,056 KB peak RSS with zero swap at
`c/magma/trace/out/test_runtime_snow_layers.time`, and the scalar guard passes
at 4,645 steps/s at
`c/magma/trace/out/perf_guard_falling_snow_cpu_1.json`. GPU 1 stayed shared
and untouched; CUDA/Blaze evidence remains deferred.
Farmland and cake are now exact centered falling supports. Dry and moisture-7
farmland both clip at y=77.9375 and drop on update 10 without changing
moisture or trampling; whole cake clips at y=77.5 and drops on update 12. The
old proof fence rejects both support classes at
`c/magma/trace/out/matrix_falling_cake_farmland_red_1/summary.md`. All three
corrected rows and nine repeats pass at
`c/magma/trace/out/matrix_falling_cake_farmland_candidate_1/summary.md` and
`c/magma/trace/out/matrix_falling_cake_farmland_repeat_3/summary.md`; all ten
shaped supports remain behavior/block exact at
`c/magma/trace/out/matrix_falling_cake_farmland_shaped_family/summary.md`.

The parked oracle agrees across 486 updates and 44 sand/gravel cases at
`c/magma/trace/out/test_falling_drop_cake_farmland.log`. The final native suite
passes in 5:49.79 at 354,736 KB peak RSS with zero swap at
`c/magma/trace/out/test_runtime_cake_farmland.time`; the scalar guard passes at
5,088 steps/s at
`c/magma/trace/out/perf_guard_falling_cake_farmland_cpu_1.json`. GPU 1 stayed
shared and untouched.
`doEntityDrops=false` is now exact for represented falling-block failed
placement. The authoritative snapshot and capsule carry the independent
global gamerule into the runtime. Sand on a bottom slab keeps the ordinary
trajectory through update 11, disappears on update 12, removes only its source
block, leaves the slab unchanged, creates no item, keeps `Math.random`
unchanged, and advances the entity-ID cursor only for the falling entity. The
old branch's isolated failure is at
`c/magma/trace/out/matrix_falling_entitydrops_false_red_1/summary.md`; the
strict corrected row and three repeats pass at
`c/magma/trace/out/matrix_falling_entitydrops_false_candidate_2/summary.md` and
`c/magma/trace/out/matrix_falling_entitydrops_false_repeat_3/summary.md`. All
11 affected shaped-support cases retain green behavior and block gates at
`c/magma/trace/out/matrix_falling_entitydrops_family/summary.md`.

The parked Java/C oracle agrees on 510 falling updates and 46 sand/gravel
cases at `c/magma/trace/out/test_falling_drop_entitydrops_false.log`. The full
native aggregate passes in 6:35.42 at 355,964 KB peak RSS with zero major
faults and zero swap at `c/magma/trace/out/test_runtime_entitydrops.time`; the
clean scalar guard passes at 4,487 steps/s at
`c/magma/trace/out/perf_guard_falling_entitydrops_cpu_1.json`. This closes only
the falling failed-placement caller. Other `doEntityDrops` callers and falling
movement behavior remain open; lateral movement is the next bounded falling
seam. GPU 1 stayed shared and untouched.

Falling-block timeout is exact for the represented sand fixture. The parked
Java/C oracle covers both out-of-height predicates, the independent
`fallTime > 600` predicate, and `doEntityDrops` true/false. All six cases agree
on post-move position and velocity, retirement, unchanged world blocks, the
first item tick where present, `Math.random`, and the next entity ID at
`c/magma/trace/out/test_falling_timeout.log`. The deliberate no-timeout build
fails at `c/magma/trace/out/test_falling_timeout_red.log`, and all 46 prior
shaped-support cases remain exact at
`c/magma/trace/out/test_falling_drop_timeout_refactor.log`. Native pool
pressure retires an expired entity even when the bounded item table cannot
represent its Java drop; the item omission and unchanged cursors under that
artificial capacity limit remain a declared resource divergence, not an
oracle parity claim. The final aggregate passes in 5:33.15 at 359,180 KB peak
RSS with zero swap at
`c/magma/trace/out/test_runtime_falling_timeout_2.time`; the scalar guard
passes at 5,031 steps/s at
`c/magma/trace/out/perf_guard_falling_timeout_cpu_1.json`. Lateral swept
collision and dynamic landing-cell selection are now exact for a free
diagonal path and an X-wall path. The retained absolute entity AABB matches
Java's large-coordinate `resetPositionToBB` rounding, Y-X-Z collision order,
flags, velocity, float fall distance, and dynamic final cell across 26 parked
updates at
`c/magma/trace/out/test_falling_lateral_ordered_final.log` and
`c/magma/trace/out/test_falling_lateral_ordered_repeat.log`. A same-time
stone callback precedes the landed sand callback in both engines, proving
relative queue order as well as `+2` delay and priority. The fixture refuses
pre-existing scheduled work in its X/Z footprint, avoiding destructive
remove/reinsert renumbering.

The affected falling-drop oracle also establishes the pressure-plate boundary:
IDs 70/72/147/148 have null entity collision but are nonreplaceable. Sand and
gravel preserve the plate and create the exact item; wood and both weighted
plates activate to metadata one, while stone excludes the nonliving falling
entity. Correcting the live-item null-collision classification prevents an
incorrect full-block upward snap. The expanded family passes 614 updates and
54 cases at
`c/magma/trace/out/test_falling_drop_pressure_plate_final_3.log`; the six
timeout cases remain green at
`c/magma/trace/out/test_falling_timeout_after_bbox.log`.

The full native aggregate passes in 4:59.15 at 359,648 KB peak RSS with zero
swap at `c/magma/trace/out/test_runtime_falling_lateral_2.time`; scalar
throughput passes at 4,986 steps/s against the 3,858.9 floor at
`c/magma/trace/out/perf_guard_falling_lateral_cpu_1.json`. This is not general
collision parity: Z-only/corner, simultaneous vertical-horizontal,
partial-height lateral obstacles, moving pistons, lateral failed placement and
fixed-pool pressure, anvils, and dynamic fluid columns remain open.
Scheduled dragon-egg falling is now exact for the represented loaded-world
slice. Placement and ordinary support loss queue one de-duplicated `+5`
callback; a supported callback drains without state changes, while an
unsupported egg creates one metadata-0 falling entity, removes its source on
the first entity update, lands after the exact 13-row trajectory, and queues
the landed egg's fresh `+5` callback. Java and C pass the supported/fall pair
three consecutive times at
`c/magma/trace/out/test_falling_dragon_egg_candidate_2.log` and
`c/magma/trace/out/test_falling_dragon_egg_repeat_2.log`. Capsule restore
admits both supported callbacks and air-below callbacks with a bounded landing
proof; its final self-test passes at
`c/magma/trace/out/state_capsule_dragon_egg_final_2.log`.

The native fixed-pool negative is intentionally bounded: when all 16 falling
slots are occupied, the callback drains but leaves the source egg and cursors
unchanged. That policy passes at
`c/magma/trace/out/test_falling_dragon_egg_capacity.log`; Java has no matching
fixed capacity. The aggregate passes in 4:59.26 at 365,004 KB peak RSS with
zero swap at `c/magma/trace/out/test_runtime_dragon_egg_final_2.time`, and the
CPU guard passes at 5,081 steps/s at
`c/magma/trace/out/perf_guard_falling_dragon_egg_cpu_1.json`. Click teleport,
unloaded/instant relocation, scheduled-table exhaustion, and
the remaining lateral/dynamic-fluid shapes stay open. GPU 1 was not used.

Scheduled anvil falling is exact for the isolated loaded-world impact slice.
Placement and support loss retain the first `+2` callback; canonical metadata
0..11 is carried through the falling entity, whose source disappears on update
one and whose centered stone-floor landing occurs on update 13. The impact
path retains its own captured 48-bit `Entity.rand`: high-roll cases preserve
metas 0, 1, 4, and 8; seed zero consumes one `nextFloat`, advances meta 0 to 4,
and breaks meta 8 without a block or item. Java and C agree on all 78 updates,
final volumes, schedules, collision/fall-distance fields, and every RNG/EID
cursor at
`c/magma/trace/out/test_falling_anvil_after_restore_fence.log`; three prior
full repeats pass at
`c/magma/trace/out/test_falling_anvil_repeat_3.log`.

Failed placement on a bottom stone slab is also exact. The anvil lands in the
occupied slab cell on update 14 with a 3.36419439 pre-impact distance, consumes
one entity `nextFloat`, and creates an item when the controlled high roll does
not advance its damage tier. Item metadata is the current damage tier rather
than raw blockstate metadata: inputs 0/1/4/8 emit 0/0/1/2. The comparator also
pins all four `Math.random` doubles, the added entity ID, constructor and
first-tick kinematics, stack, age, and pickup delay. It passes 134 updates and
11 cases twice at `c/magma/trace/out/test_falling_anvil_drop_1.log` and
`c/magma/trace/out/test_falling_anvil_drop_repeat.log`.

The explicit `BlockFalling.fallInstantly` worldgen mode is exact for the
bounded represented-floor column. It removes the source, scans vertically,
places the original full state above the first blocker, and schedules the
landed block without constructing an entity or consuming entity/RNG cursors.
Anvil metas 0/1/4/8 agree, and native coverage applies the same generic path to
metadata-0 sand. The expanded 15-case comparator passes consecutively at
`c/magma/trace/out/test_falling_instant_final_1.log` and
`c/magma/trace/out/test_falling_instant_final_2.log`; public callback restore
passes at
`c/magma/trace/out/test_falling_instant_public_restore_repeat.log`.

Bounded fresh-player impact damage is now exact. The controlled player is
unarmored, has no effects or absorption, is moved into the correct vertical
chunk list, and intersects the landed anvil on update 13. Both engines derive
impact two from the 2.902239-block pre-impact distance, apply raw damage four,
move health 20 to 16, set hurt resistance 20 and hurt time 10, record last
damage four, and add 0.1 food exhaustion. Java's source-less hurt direction
also consumes one `Math.random()` `nextDouble`, or two 48-bit LCG steps,
before the anvil consumes its independent `Entity.rand.nextFloat`; magma now
preserves that ordering.

The expanded comparator passes 147 updates and 16 cases twice at
`c/magma/trace/out/test_falling_anvil_impact_final_1.log` and
`c/magma/trace/out/test_falling_anvil_impact_final_2.log`. Existing no-target,
damage-tier, break, drop, and instant controls pass in the same runs. The full
native aggregate passes in 5:02.51 at 372,576 KB peak RSS, zero major faults,
and zero swap at `c/magma/trace/out/test_runtime_anvil_impact.log` and `.time`.
The Java client was stopped for the scalar guard, which passes at 4,998
steps/s against the 3,858.9 floor at
`c/magma/trace/out/perf_guard_falling_anvil_impact_cpu_1.json`. The target path
is reached only for an active falling anvil at impact and adds no idle scan or
allocation.

The two hurt-immunity branches are exact at that same boundary. With an active
20/10 window and prior raw damage four, the incoming raw-four anvil hit is
rejected: health stays 20, exhaustion stays zero, timers and last damage stay
unchanged, and neither player nor global hurt-direction RNG runs. With prior
raw damage two, only the two-point difference is accepted: health becomes 18,
last damage becomes four, and exhaustion becomes 0.1 while the 20/10 timers
remain. This delta branch also skips fresh-hurt direction/status/sound work.
Both cases still consume the anvil entity's independent degradation
`nextFloat` after target iteration.

The expanded Java/C suite passes 173 updates and 18 cases twice at
`c/magma/trace/out/test_falling_anvil_immunity_final_1.log` and
`c/magma/trace/out/test_falling_anvil_immunity_final_2.log`. The native
aggregate passes in 5:22.12 at 378,148 KB peak RSS with zero major faults and
zero swap at `c/magma/trace/out/test_runtime_anvil_immunity.log` and `.time`.
The stopped-oracle scalar guard passes at 5,089 steps/s at
`c/magma/trace/out/perf_guard_falling_anvil_immunity_cpu_1.json`.

The fresh fully absorbed boundary is also exact. Four absorption points consume
all four raw anvil damage while health stays 20 and food exhaustion stays zero;
the accepted hit still sets hurt resistance 20, hurt time 10, last damage four,
and consumes Java's source-less `Math.random()` `nextDouble`. The expanded
comparator passes 186 updates and 19 cases twice at
`c/magma/trace/out/test_falling_anvil_absorption_final_1.log` and
`c/magma/trace/out/test_falling_anvil_absorption_final_2.log`. Its fixture
captures the controlled global Math cursor at the terminal impact boundary and
uses exact falling/item IDs for the controlled Entity-ID transition, isolating
the proof from unrelated client constructors in the same integrated JVM.

The native aggregate passes in 5:16.55 at 375,032 KB peak RSS with zero major
faults and zero swap at
`c/magma/trace/out/test_runtime_anvil_absorption.log` and `.time`. The CPU
product and Java oracle build, and the stopped-oracle scalar guard passes at
4,948 steps/s against the 3,858.9 floor at
`c/magma/trace/out/perf_guard_falling_anvil_absorption_cpu_1.json`.

Resistance I is exact at the same fresh-player impact boundary. Amplifier zero
reduces the post-armor damage from four to the float value 3.2, so health ends
at 16.8 with 0.1 exhaustion, while raw `lastDamage` remains four and the 20/10
hurt pair opens normally. The 20-case Java/C comparator passes 199 updates in
consecutive runs at
`c/magma/trace/out/test_falling_anvil_resistance_final_1.log` and
`c/magma/trace/out/test_falling_anvil_resistance_final_2.log`.

The full native aggregate passes in 5:04.39 at 380,944 KB peak RSS, zero major
faults, and zero swap at
`c/magma/trace/out/test_runtime_anvil_resistance.log` and `.time`. Java and the
CPU product build, and the stopped-oracle scalar guard passes at 5,072 steps/s
against the 3,858.9 floor at
`c/magma/trace/out/perf_guard_falling_anvil_resistance_cpu_1.json`. This slice
required no product change because the represented potion kernel was already
correct.

A controlled NoAI pig is now exact as the first bounded non-player anvil
target. From health 10, the raw-four hit leaves health 6 and raw last damage
four. Target RNG advances by one `nextDouble` plus two `nextFloat` calls,
global `Math.random` advances by the source-less hurt-direction `nextDouble`,
and falling RNG advances by its later degradation `nextFloat`. Entity IDs,
world RNG, trajectory, landing, and scheduling are pinned as well.

The cold fixture now calls the same magma falling-entity phase directly, so
Java and C compare the immediate 20/10 hurt timers exactly. The ordinary
public runtime continues into the controlled-mob phase and stores 19/9; native
coverage checks that post-tick boundary separately. The strengthened fixture
also covers two freshly inserted NoAI pigs in one chunk section. Java returns
them in chunk-section insertion order and magma visits the two fresh ascending
slots in the same order. Distinct target RNG seeds, ordered logical IDs, both
health results, all timers, raw damage, global/target/falling/world RNG,
trajectory, landing, and scheduling are exact.

The 22-case suite passes 225 updates twice at
`c/magma/trace/out/test_falling_anvil_pigs_final_1.log` and
`c/magma/trace/out/test_falling_anvil_pigs_final_2.log`. The native aggregate
passes in 5:22.90 at 385,692 KB peak RSS with zero major faults at
`c/magma/trace/out/test_runtime_anvil_pigs.log` and `.time`. The stopped-oracle
CPU guard passes at 5,069 steps/s at
`c/magma/trace/out/perf_guard_falling_anvil_pigs_cpu_1.json`. This closes only
controlled pigs with fresh slot order. Slot reuse, other living types, natural
AI reactions, armor/effects, general multi-target ordering, knockback, drops,
sound/events, and broader mob state remain open.

A controlled NoAI cow is exact at the same immediate impact boundary. Cow
health moves 10 to 6 with 20/10 timers and raw last damage four. Its pinned
Entity.rand advances four LCG steps, its constructor and source-less hurt
direction advance global Math eight steps in total, the falling entity advances
once, and world RNG is unchanged. Query membership, logical ID, trajectory,
landing, and scheduling are also pinned. At this promotion boundary the
product predicate admitted controlled pigs and cows; the subsequent sheep
slice below widens that bounded roster.

The 23-case suite passes 238 updates twice at
`c/magma/trace/out/test_falling_anvil_cow_final_1.log` and
`c/magma/trace/out/test_falling_anvil_cow_final_2.log`. The native aggregate
passes in 5:18.17 at 385,956 KB peak RSS with zero major faults at
`c/magma/trace/out/test_runtime_anvil_cow.log` and `.time`. The CPU guard
passes at 5,052 steps/s at
`c/magma/trace/out/perf_guard_falling_anvil_cow_cpu_1.json`. Sheep, chickens,
hostiles, natural AI reactions, equipment/effects, slot reuse, general target
ordering, drops, knockback, and emitted events remain open.

A survival player wearing one plain undamaged diamond chestplate is exact at
the same fresh impact boundary. Armor eight and toughness two reduce raw four
to exact health 17.02400016784668 (`0x41883127`); slot 38 remains item 311,
count one, with durability advanced from zero to one. Hurt timers are 20/10,
raw last damage is four, exhaustion is 0.1, and the other armor slots remain
empty.

The comparator now also pins the player `Entity.rand` cursor in all six
represented player modes. Fresh ordinary, absorption, Resistance-I, and
chestplate hits advance four LCG steps for `setBeenAttacked` plus hurt-sound
pitch. Equal rejection and the stronger immunity-window delta advance none.
Global Math, world, falling, ID, trajectory, landing, schedule, and inventory
state remain independently exact.

The 24-case suite passes 251 updates twice at
`c/magma/trace/out/test_falling_anvil_armor_final_1.log` and
`c/magma/trace/out/test_falling_anvil_armor_final_2.log`. The native aggregate
passes in 5:02.36 at 387,440 KB peak RSS with zero major faults and zero swap
at `c/magma/trace/out/test_runtime_anvil_armor.log` and `.time`. The CPU guard
passes at 5,143 steps/s at
`c/magma/trace/out/perf_guard_falling_anvil_armor_cpu_1.json`. This proof does
not cover enchanted or multiple armor pieces, creative durability immunity,
or general ANVIL source flags.

The plain diamond-helmet pre-hook is now exact. Java damages a nonempty head
slot before the hurt gate with `int(raw*4 + nextFloat*raw*2)` and scales the
incoming amount by 0.75. At raw four and the pinned player seed, the special
draw contributes durability 19; ordinary armor contributes one more. The
helmet remains item 310/count one/meta 20, `lastDamage` is three, health is
17.215999603271484 (`0x4189ba5e`), timers are 20/10, and exhaustion is 0.1.
Player RNG advances five LCG steps, global Math two, and falling RNG one.

The 25-case suite passes 264 updates twice at
`c/magma/trace/out/test_falling_anvil_helmet_final_1.log` and
`c/magma/trace/out/test_falling_anvil_helmet_final_2.log`. The native aggregate
passes in 4:59.85 at 393,744 KB peak RSS with zero major faults and zero swap
at `c/magma/trace/out/test_runtime_anvil_helmet.log` and `.time`; the CPU guard
passes at 5,151 steps/s at
`c/magma/trace/out/perf_guard_falling_anvil_helmet_cpu_1.json`. Enchanted
Unbreaking RNG, near-break/removal, and item-break effects remain open.

A controlled NoAI sheep is now exact. The product and fixture use vanilla's
eight-point max health and actual 0.9 by 1.3 collision box instead of the prior
generic-passive ten health and cow-height 1.4 approximation. The Java and C
JSON both expose the target AABB, preventing the centered overlap from hiding
that difference. Raw four damage leaves health four with immediate 20/10
timers and last damage four. Sheep RNG advances four LCG steps, constructor
plus source-less direction advance global Math eight, falling RNG advances
once, and world RNG is unchanged.

The 26-case suite passes 277 updates twice at
`c/magma/trace/out/test_falling_anvil_sheep_final_1.log` and
`c/magma/trace/out/test_falling_anvil_sheep_final_2.log`. The native aggregate
includes ordered pig/pig/cow/sheep public-tick coverage and passes in 5:03.68
at 252,404 KB peak RSS with zero major faults and zero swap at
`c/magma/trace/out/test_runtime_anvil_sheep.log`. The stopped-oracle CPU guard
passes at 4,916 steps/s against the 3,858.9 floor at
`c/magma/trace/out/perf_guard_falling_anvil_sheep_cpu_1.json`. The subsequent
no-loot chicken death boundary is covered below; hostiles, natural AI,
equipment/effects, slot reuse, general ordering, and emitted events remain
open.

A controlled NoAI chicken is exact for the first lethal target boundary with
`doMobLoot=false`. Chicken max health four and its 0.4 by 0.7 target AABB are
explicit. Immediately after falling impact, health is zero, the protected
living-death flag is true, `Entity.isDead` is false, death time remains zero,
hurt state is 20/10 with last damage four, and there are no new item or XP
entities. Chicken RNG advances four LCG steps, constructor plus null-source
direction advance global Math eight, falling RNG advances one, world RNG is
unchanged, and the logical ID cursor advances only for falling block plus
chicken.

The initial full native run found that the ordinary mobs-enabled loop aged the
hurt timers but omitted the same-tick controlled death update. It now advances
zero-health controlled targets before AI/travel: the direct impact proof stays
at death time zero, while the public falling-then-chicken tick reaches death
time one. The ordered native fixture covers pig/pig/cow/sheep/chicken and
rejects the old omission.

The current-source 27-case suite passes 290 updates twice at
`c/magma/trace/out/test_falling_anvil_chicken_final_3.log` and
`c/magma/trace/out/test_falling_anvil_chicken_final_4.log`. The corrected
native aggregate passes in 5:05.39 at 252,668 KB peak RSS with zero major
faults and zero swap at
`c/magma/trace/out/test_runtime_anvil_chicken_final.log`; the CPU guard passes
at 5,157 steps/s against the 3,858.9 floor at
`c/magma/trace/out/perf_guard_falling_anvil_chicken_cpu_1.json`.

The adjacent `doMobLoot=true` slice covers one exact lethal chicken loot-table
roll. Vanilla's ordered feather and raw-chicken pools produce item 288 count
two, then item 365 count one. Target RNG advances seven LCG steps; global Math
advances 24 for the chicken constructor, null-source direction, and two item
constructors; falling RNG advances once; world RNG is unchanged; and IDs
advance through falling/chicken/feather/chicken. Both items compare exact
position, motion, yaw, hover phase, stack, age, pickup delay, health, lifespan,
ground/dead state, plus their next public tick.

The integrated Java fixture must keep the controlled chicken server-only.
Otherwise its client mirror constructor consumes six draws from the same
process-global `Math.random` and contaminates the server item constructors.
With that oracle artifact removed, two fresh-JVM 28-case runs pass all 303
updates at
`c/magma/trace/out/test_falling_anvil_chicken_loot_final_1.log` and
`c/magma/trace/out/test_falling_anvil_chicken_loot_final_2.log`. The native
aggregate passes in 4:25.85 at 253,480 KB peak RSS, zero major faults, and zero
swap at
`c/magma/trace/out/test_runtime_anvil_chicken_loot_final.log`; the CPU guard
passes at 5,180 steps/s against the 3,858.9 floor at
`c/magma/trace/out/perf_guard_falling_anvil_chicken_loot_cpu_1.json`.

Pinned target cursors now cover feather counts zero, one, and two. Zero
feathers produces one meat entity, advances global Math 16 total steps, and
advances the ID cursor three; one/two feathers produce two entities, advance
global Math 24, and advance IDs four. A fresh Java chicken's exact inactive
fire counter is `-1`; a second fixture pins fire 100 and proves the loot
table's `EntityOnFire` furnace-smelt condition changes raw item 365 to cooked
366 without consuming RNG. Both fire counter and burning predicate are compared.

Two fresh-JVM 31-case runs pass all 342 updates at
`c/magma/trace/out/test_falling_anvil_chicken_cooked_final_1.log` and
`c/magma/trace/out/test_falling_anvil_chicken_cooked_final_2.log`. The native
aggregate passes in 5:02.42 at 253,492 KB peak RSS, zero major faults, and zero
swap at
`c/magma/trace/out/test_runtime_anvil_chicken_cooked_final.log`; the CPU guard
passes at 5,088 steps/s against the 3,858.9 floor at
`c/magma/trace/out/perf_guard_falling_anvil_chicken_cooked_cpu_1.json`.

The same cooked fixture now covers controlled fire-tick progression and the
complete 20-tick death lifecycle. Fire ages from 100 to 80; death time reaches
20; the target is removed only at tick 20; its two loot entities reach age 20
and pickup delay zero; and the null-attacker fixture creates no XP. The
terminal particle loop also consumes the exact target-local RNG stream. Its
60 Gaussian calls use Java's cached partner and rejection loop, so this pinned
seed advances 200 LCG steps during the burst and 207 total after impact.

Three fresh-JVM gates pass 362 exact rows at
`c/magma/trace/out/test_falling_anvil_chicken_post_final_1.log` through
`c/magma/trace/out/test_falling_anvil_chicken_post_final_3.log`. The final
native aggregate passes in 5:43.15 at 401,080 KB peak RSS with zero major
faults and zero swap at
`c/magma/trace/out/test_runtime_anvil_chicken_post_final.log`; the stopped-oracle
CPU guard passes at 5,059 steps/s against the 3,858.9 floor at
`c/magma/trace/out/perf_guard_falling_anvil_chicken_post_cpu_1.json`.

The immediate chicken status and sound boundary is exact as an ordered causal
stream. Server mixins capture the true status calls and the final post-Forge
sound payload. All five lethal fixtures emit status 2, namespaced chicken death
sound with exact category/position/volume/pitch, then status 3 after loot. The
native nonlethal and hurt-resistance controls independently prove hurt sound
and status-3-only behavior. A fixed 285-record ring exposes monotonic sequence
and overwrite count; exact fill plus one-record wrap passes natively.

Two uncontaminated fresh-JVM gates pass 362 exact rows and event payloads at
`c/magma/trace/out/test_falling_anvil_chicken_events_final_1.log` and
`c/magma/trace/out/test_falling_anvil_chicken_events_final_3.log`. The native
aggregate passes in 5:02.85 at 253,536 KB peak RSS with zero major faults and
zero swap at
`c/magma/trace/out/test_runtime_anvil_chicken_events_final.log`; the CPU guard
passes at 5,169 steps/s against the 3,858.9 floor at
`c/magma/trace/out/perf_guard_falling_anvil_chicken_events_cpu_1.json`.

Pig, cow, and sheep now share the exact observed stream. Fresh nonlethal hits
emit status 2 and their namespaced hurt sound with exact neutral category,
position, target-derived pitch, and source volume. Cow is the important
override at volume 0.4. Two overlapping pigs are attributed by an
`Entity.playSound` thread-local source stack carried into the final server
handler, so their status/sound pairs remain correctly interleaved and retain
distinct logical EIDs despite identical positions.

Two fresh-JVM full gates pass all 362 rows at
`c/magma/trace/out/test_falling_anvil_passive_events_final_1.log` and
`c/magma/trace/out/test_falling_anvil_passive_events_final_2.log`. The native
aggregate passes in 5:03.90 at 252,700 KB peak RSS with zero major faults and
zero swap at
`c/magma/trace/out/test_runtime_anvil_passive_events_final.log`; the CPU guard
passes at 5,037 steps/s against the 3,858.9 floor at
`c/magma/trace/out/perf_guard_falling_anvil_passive_events_cpu_1.json`.

The anvil landing and terminal-break world-event payloads are now exact. A
scoped real-Java `IWorldEventListener` observes event 1031 only after a
successful landing state is installed and event 1029 after damage-tier-two
breakage without a landing write. Both carry the integer landing cell and data
zero. Supported, instant, and failed-placement/drop controls emit neither.
Magma records the same payload in a separate allocation-free 16-record ring
bounded by the falling-entity pool, with monotonic sequence and overwrite
counters and no idle scan.

The six focused fall variants pass 78 exact updates at
`c/magma/trace/out/test_falling_anvil_world_events_focus.log`; the negative
controls pass at
`c/magma/trace/out/test_falling_anvil_world_events_negatives.log`. Two
fresh-JVM full gates pass 362 rows and 31 cases at
`c/magma/trace/out/test_falling_anvil_world_events_final_1.log` and
`c/magma/trace/out/test_falling_anvil_world_events_final_2.log`. The native
family passes in 6:09.52 at 405,236 KB peak RSS with zero swap at
`c/magma/trace/out/test_runtime_anvil_world_events_final.log`; the CPU guard
passes at 5,004 steps/s against the 3,858.9 floor at
`c/magma/trace/out/perf_guard_falling_anvil_world_events_cpu_1.json`.

The terminal controlled-chicken particle callback payload is exact. A scoped
real-Java `IWorldEventListener` observes 20 ID-zero `EXPLOSION_NORMAL`
particles only at death tick 20, with `ignoreRange=true`, no parameters, and
all position/velocity doubles serialized as raw bits. The comparator requires
zero batches through ticks one to 19 and bit-identical payloads at tick 20.
The product preserves Java's per-particle Gaussian/Gaussian/Gaussian then
float/float/float draw order and float-to-double widening for chicken width
0.4 and height 0.7.

Host libm initially left four Gaussian velocity components one to three ULP
from Java. `jrand_gaussian_next` now uses the fdlibm `StrictMath.log`
evaluation order in shared host/device source. The separate `entity_random`
Java/CPU oracle remains exact for all 17 outputs, and the CUDA translation
compiles for `sm_120` with `--fmad=false`; GPU execution is deferred while GPU
1 is shared.

The fixed 95-batch product ring holds one atomic 20-particle batch per
represented living slot and exposes monotonic sequence and overwrite counts.
Native coverage proves exact fill and one-batch wrap without allocation. Two
fresh-JVM full gates pass 362 rows and 31 cases at
`c/magma/trace/out/test_falling_anvil_terminal_particles_final_1.log` and
`c/magma/trace/out/test_falling_anvil_terminal_particles_final_2.log`. The
native family passes in 6:28.65 at 433,092 KB peak RSS with zero swap at
`c/magma/trace/out/test_runtime_terminal_particles.log`; the stopped-oracle
CPU guard passes at 5,115 steps/s against the 3,858.9 floor at
`c/magma/trace/out/perf_guard_falling_anvil_terminal_particles_cpu_1.json`.

Player-credit adult-chicken XP is exact at this boundary. A saved
`recentlyHit=20` reaches death tick 20 with entry value one, consumes one
`World.rand.nextInt(3)`, returns three for the pinned cursor, and creates one
unsplit orb before terminal particles. The comparator bit-checks the orb's
four-double Math.random constructor and same-world-tick gravity/move/drag
update, plus private health, value, age, pickup delay, color cursors, entity
ID, and all global RNG cursors. The `recentlyHit=19` negative expires before
the terminal boundary and consumes none of them. A full fixed 95-orb pool also
rejects before RNG or ID mutation. Two consecutive 428-row/33-case gates pass
at `c/magma/trace/out/test_falling_anvil_chicken_xp_final_1.log` and
`c/magma/trace/out/test_falling_anvil_chicken_xp_final_2.log`; the native and
CPU performance evidence is recorded in `docs/DEVLOG.md`.

Player-credited adult-pig loot and XP are exact through the same terminal
boundary. An unsaddled, nonburning pig at health four emits the exact lethal
status/sound/status stream, then one raw-porkchop EntityItem stack with count
three. It consumes the one-entry pool choice and `set_count` target draws,
four item-constructor `Math.random` doubles, and causal ID 520002. The complete
20-tick death sequence retains player credit, ages that item, emits pig-sized
terminal particles, and creates one value-three XP orb at causal ID 520003
with exact raw payload bits and same-tick update. Target, World, Math, and
entity-ID cursors all match.

Captured real-Java passive loot now enters the server entity/chunk lists
without a tracker mirror. Isolated item/orb updates call their real `onUpdate`
methods directly, avoiding a chunk-wrapper skip and process-global Math race
observed in discarded runs. Three focused pig repeats and two consecutive
461-row/34-case full gates pass at
`c/magma/trace/out/test_falling_anvil_pig_loot_xp_final_1.log` and
`c/magma/trace/out/test_falling_anvil_pig_loot_xp_final_2.log`. The native and
CPU performance evidence is recorded in `docs/DEVLOG.md`.

Player-credited adult-cow loot and XP are exact through the same boundary.
The two ordered fixed-roll pools consume their one-entry choices and
`set_count` draws, producing leather 334 count one followed by raw beef 363
count one for the pinned cursor. Both item entities retain exact causal IDs,
constructor payloads, timers, and stack state. The immediate target cursor is
the initial cow seed advanced eight LCG steps and global Math is advanced 24.
The exact lethal event stream uses cow's neutral volume 0.4 death sound.

The complete 20-tick lifecycle retains player credit, ages both items, and at
tick 20 emits a value-three XP orb at causal ID 520004 plus the exact 0.9 by
1.4 cow terminal-particle payload. The seed's Gaussian rejection behavior
leaves the target cursor at initial plus 216 LCG steps. Every post row, raw orb
and particle word, World/Math cursor, and final causal ID 520005 matches. The
native fixed-pool preflight reserves this seed's two item slots before any
mutation; a one-slot fixture rejects atomically. Native coverage also locks
the source-backed cooked-beef item 364 branch.

Three focused repeats and two consecutive 494-update/35-case full gates pass
at `c/magma/trace/out/test_falling_anvil_cow_loot_xp_focus_1.log` through
`c/magma/trace/out/test_falling_anvil_cow_loot_xp_focus_3.log`, and
`c/magma/trace/out/test_falling_anvil_cow_loot_xp_full_1.log` plus
`c/magma/trace/out/test_falling_anvil_cow_loot_xp_full_2.log`. Native and CPU
performance evidence passes at
`c/magma/trace/out/test_runtime_cow_loot_xp_full.log` and
`c/magma/trace/out/perf_guard_falling_anvil_cow_loot_xp_cpu_1.json` and is
summarized in `docs/DEVLOG.md`.

Adult sheep carry the exact five-bit fleece state used by Java: color metadata
0..15 plus the sheared bit. The state resets on slot reuse and reaches live
rendering and loot. Unsheared red emits wool 35:14 count one followed by raw
mutton 423 count two; its immediate target cursor is initial plus eight and
global Math is at step 24. Sheared red bypasses the color table, emits only raw
mutton 423 count two, and ends the impact at target plus six and Math step 16.
The lethal event stream remains the neutral sheep death sound at volume one.

All 20 death ticks match. Unsheared colors retain XP orb ID 520004, final ID
520005, and target plus 224. Sheared sheep use orb ID 520003, final ID 520004,
and target plus 222. World/Math cursors, item ages, credit, timers, and every
raw orb/particle word agree. Native coverage locks cooked mutton 424, validates
state bounds and slot reset, and proves an unsheared two-slot rejection is
atomic while a sheared target accepts the same single free slot.

Focused white, red, sheared, and nonlethal comparisons pass. Two consecutive
593-update/38-case full gates pass at
`c/magma/trace/out/test_falling_anvil_sheep_state_full_1.log` and
`c/magma/trace/out/test_falling_anvil_sheep_state_full_2.log`, in about two
minutes each with about 46 MB peak harness RSS and zero swap. Java, the C
product, and live/recorded sheep renderer tests pass. The complete native
aggregate passes in 5:17.19 at 253,716 KB peak RSS with zero swap at
`c/magma/trace/out/test_runtime_sheep_state_full.log`. The stopped-oracle CPU
guard passes at 4,664 steps/s against the 4,062 baseline and 3,858.9 floor at
`c/magma/trace/out/perf_guard_falling_anvil_sheep_state_cpu_1.json`.

Player shearing is now exact for the represented survival interaction slice.
The product compares the three-block expanded sheep AABB against the selected
block, sends the use to the following server tick, and supports main-hand plus
empty-main/offhand shears. Adult unsheared sheep consume `nextInt(3)`, emit the
neutral shear sound, set the sheared bit, and create one to three separate wool
entities with exact item ID/meta/count, causal IDs, pickup delay, position,
motion, yaw, hover phase, and RNG cursors. Child and already-sheared sheep are
handled no-ops. Unbreaking III consumes the sheep's entity RNG and can preserve
durability exactly. The fixed item pool preflights the required number of
separate entities and rejects insufficient capacity without state, RNG, ID,
tool, sound, or drop mutation.

Two consecutive seven-case Java-vs-magma gates pass at
`c/magma/trace/out/test_shearing_full_1.log` and
`c/magma/trace/out/test_shearing_full_2.log`, covering ten exact wool entities.
The focused native runtime test covers delayed main/offhand use, occlusion,
the child view flag, ineligible items, Unbreaking and atomic capacity. The mob
and entity-render suites pass. The complete native aggregate passes in 5:29.76
at 446,192 KB peak RSS with zero major faults and zero swap at
`c/magma/trace/out/test_runtime_shearing_full.log`. The stopped-oracle scalar guard passes at 4,788
steps/s against the 4,062 baseline and 3,858.9 floor at
`c/magma/trace/out/perf_guard_shearing_cpu_1.json`. The interaction is
input-driven and adds no idle allocation or loaded-world scan. GPU 1 was not
used.

The represented sheep grass-eating task is now exact at its Java callback and
natural product boundaries. Eligibility consumes one entity-RNG draw with
bound 1,000 for adults or 50 for children, then recognizes tallgrass meta one
at the sheep's floored position or grass below. Start emits entity status 10
and clears movement at timer 40; the start tick updates to 39, and update 36
applies at timer four. Tallgrass has priority over grass below. Destruction
emits world event 2001 with state data 4127 for tallgrass or block ID 2 for
grass. With `mobGriefing=false`, neither block nor event changes, but the same
bonus clears the sheared bit and advances a child by 1,200 age ticks with the
vanilla zero clamp.

The live product keeps per-sheep scheduler and timer state, checks starts on
the three-tick goal cadence, interrupts for represented panic, suppresses
movement during the task, advances ordinary passive age, and excludes
`NoAI` fixtures. Timer-derived head rotation-point Y and pitch now reach live
entity views at tick boundaries. Two consecutive 11-case strict comparisons
pass at `c/magma/trace/out/test_grazing_full_1.log` and
`c/magma/trace/out/test_grazing_full_2.log`, each in 3.2 seconds at 30,252 KB
peak RSS and zero swap. The focused product gate passes at
`c/magma/trace/out/test_grazing_runtime.log`. The first aggregate correctly
failed when the new goal consumed RNG on a `NoAI` anvil fixture; after gating
the task on AI-enabled state, the full aggregate passes in 5:23.18 at 445,928
KB peak RSS, zero major faults, and zero swap at
`c/magma/trace/out/test_runtime_grazing_full_2.log`. The CPU guard passes at
4,718 steps/s against the 3,858.9 floor at
`c/magma/trace/out/perf_guard_grazing_cpu_1.json`. GPU 1 was not used.

Natural sheep fleece selection is now exact at both the selector and product
spawn boundary. `EntitySheep.getRandomSheepColor` consumes one
`World.rand.nextInt(100)`: rolls 0..4 select black metadata 15, 5..9 gray 7,
10..14 silver 8, and 15..17 brown 12. Only rolls 18..99 consume a second
`nextInt(500)`; zero selects pink metadata 6 and every other value selects
white metadata 0. The native `onInitialSpawn` hook writes only the low nibble,
preserving the sheared bit, and automatic passive sheep creation advances the
shared runtime World cursor. Entity construction, explicit cold fixtures, and
NBT reload do not reroll fleece.

Two consecutive 13-comparison Java-vs-magma gates cover all six results,
one-step and two-step final RNG cursors, static selection, real Java
`onInitialSpawn`, and sheared-bit preservation at
`c/magma/trace/out/test_sheep_color_full_1.log` and
`c/magma/trace/out/test_sheep_color_full_2.log`. The focused native gate also
drives an ordinary passive sheep spawn and passes in 0.02 seconds at 18,756 KB
peak RSS and zero swap at
`c/magma/trace/out/test_sheep_color_runtime.log`. The complete aggregate passes
in 5:50.27 at 445,892 KB peak RSS, zero major faults, and zero swap at
`c/magma/trace/out/test_runtime_sheep_color_full.log`. The CPU guard passes at
4,598 steps/s against the 3,858.9 floor at
`c/magma/trace/out/perf_guard_sheep_color_cpu_1.json`. There is no new idle
scan or allocation, and a created sheep costs at most two Java LCG advances.
GPU 1 was not used.

Represented-sheep feeding and the direct mate/birth callback are now exact.
Wheat use covers adult 600-tick love state, status 18, player
attribution, survival and creative consumption, child `ageUp` and forced-age
state, delayed main/offhand order, and solid-block occlusion. Explicit Java
32-bit arithmetic preserves the `Integer.MIN_VALUE` wrap boundary without C
signed overflow. The 16 by 16
parent-fleece matrix matches every crafting recipe and the exact fallback
`World.rand` cursor. The direct mate callback requires distance squared below
nine and births on update 60. It matches parent cooldown/love reset, child age
-24000 and fleece,
child and XP IDs, constructor RNG, seven heart particles and Gaussian cache,
`doMobLoot`, Forge cancellation, and null-child ordering. The ordinary product
scheduler is native-tested for nearest compatible selection on its bounded
three-tick cadence, panic and mate-over-graze priority, and reset timing. One
isolated high-air world-tick boundary is now exact as well: the real Java
`World.updateEntities` live-list loop starts the mate task, births on that
update, appends child then XP, and reaches both new entities later in the same
tick. Magma matches that exact append order, first physics/AI/age update,
counters, RNG/Gaussian state, particles, cursors, and IDs for recipe/fallback
color and `doMobLoot` on/off cases.

Two consecutive feeding, genetics, and mating gates pass at
`c/magma/trace/out/test_sheep_feed_full_3.log`,
`c/magma/trace/out/test_sheep_feed_full_4.log`,
`c/magma/trace/out/test_sheep_genetics_full_1.log`,
`c/magma/trace/out/test_sheep_genetics_full_2.log`,
`c/magma/trace/out/test_sheep_mating_full_3.log`, and
`c/magma/trace/out/test_sheep_mating_full_4.log`. They cover 13 feeding cases,
including Java-wrapped extreme child ages, 512 genetics rows, and eight
complete mating lifecycles. The focused native
gate also covers ordinary scheduling, resets, and both fixed-capacity
fallbacks at `c/magma/trace/out/test_sheep_mating_runtime.log`. If the living
store is full, the child is counted as dropped while parent state, heart
particles, XP, RNG, and IDs still advance. If the XP store is full, the orb is
counted as dropped after its ID and constructor RNG are consumed, without
overwriting an existing orb. The final-source complete native aggregate passes
in 5:47.68 at 446,060 KB peak RSS with zero major faults and zero swap at
`c/magma/trace/out/test_runtime_sheep_mating_full_final.log`. The stopped-oracle
CPU guard passes at 4,804 steps/s against the 3,858.9 floor at
`c/magma/trace/out/perf_guard_sheep_mating_cpu_final.json`. The inactive path has
no allocation; only in-love represented sheep perform the bounded mate scan.
GPU 1 was not used.

The bounded full-tick fixture passes three exact Java-vs-magma cases twice at
`c/magma/trace/out/test_sheep_mating_tick_full_1.log` and
`c/magma/trace/out/test_sheep_mating_tick_full_2.log`, and again after the
legacy falling-anvil expectation update at
`c/magma/trace/out/test_sheep_mating_tick_after_anvil.log`. The final
header-rebuilt comparison passes at
`c/magma/trace/out/test_sheep_mating_tick_final_source.log`. The isolated anvil
oracle still passes separately, proving that its falling-entity-only contract
was not weakened. The focused ASan/UBSan run passes at
`c/magma/trace/out/test_sheep_mating_tick_sanitized_final.log`; the shared
`Random.nextLong` helper found by that run now avoids signed-shift UB and still
passes its 17-line Java/CPU/CUDA oracle. The final header-rebuilt native
aggregate passes in 6:17.39 at 446,552 KB peak RSS and zero swap at
`c/magma/trace/out/test_runtime_sheep_mating_tick_full_final_source.log`.
The stopped-oracle scalar guard passes at 4,583 steps/s against the 3,858.9
floor at `c/magma/trace/out/perf_guard_sheep_mating_tick_cpu_final.json`.

The full-tick promotion is deliberately bounded. It covers one compatible
pair at 0, 0.25, 1.0, 2.0, or 2.75 blocks, a pinned valid newborn
`Entity.rand` save state, the observed negative wander/watch/idle task path,
and at most one child in the boundary. The seven-case expansion proves the
airborne PathNavigateGround no-body-movement result, overlapping adult/child
push order and bits, fresh-child ungrounded state, and child half-size movement
physics at
`c/magma/trace/out/test_sheep_mating_collision_final.log` and
`c/magma/trace/out/test_sheep_mating_collision_final_2.log`. The focused
ASan/UBSan run passes at
`c/magma/trace/out/test_sheep_mating_collision_sanitized_final.log`; the
final-source native aggregate passes in 5:43.70 at 446,296 KB peak RSS, zero
major faults, and zero swap at
`c/magma/trace/out/test_runtime_sheep_mating_collision_final.log`. With the
oracle stopped, scalar throughput passes at 4,837 steps/s against the 3,858.9
floor at `c/magma/trace/out/perf_guard_sheep_mating_collision_cpu_final.json`.

The ten-case grounded-navigation expansion keeps all seven controls and adds
unobstructed axial, diagonal, and 90-degree-clamped first steps. The grounded
rows use a temporary isolated stone platform and match Java's integer
PathPoint center, table-based `MathHelper.atan2`, MoveHelper angle limiting,
movement-speed application, travel, collision, child/XP updates, IDs, and RNG
cursors bit-for-bit. To prevent the integrated client from contaminating the
server-only save state, the fixture restores Math/EID cursors at the causal
`EntityAIMate.spawnBaby` HEAD and validates exactly one pin. It does not reset
away any child or XP constructor draws. Full comparisons pass before and
after a cold oracle restart at
`c/magma/trace/out/test_sheep_mating_grounded_navigation.log` and
`c/magma/trace/out/test_sheep_mating_grounded_navigation_restart.log`. The
focused ASan/UBSan runtime passes at
`c/magma/trace/out/test_sheep_mating_grounded_navigation_sanitized.log`; the
broad native aggregate passes in 6:40.87 at 446,224 KB peak RSS, zero major
faults, and zero swap at
`c/magma/trace/out/test_runtime_sheep_mating_grounded_navigation.log`. Two
stopped-oracle scalar guards pass at 4,339 and 5,018 steps/s against the
3,858.9 floor at
`c/magma/trace/out/perf_guard_sheep_mating_grounded_navigation_cpu.json` and
`c/magma/trace/out/perf_guard_sheep_mating_grounded_navigation_cpu_2.json`.
The exact branch has fixed storage, no allocation, and no CUDA/GPU change.

The twelve-case expansion proves the bounded dynamic tail for two simultaneous
births. Pair A alone restores the fixture Math/EID cursor, while pair B inherits
the causal child and optional XP constructor consumption. Exact comparisons
pass before and after a cold restart at
`c/magma/trace/out/test_sheep_mating_simultaneous_order.log` and
`c/magma/trace/out/test_sheep_mating_simultaneous_order_restart.log`; the
final-source rerun is
`c/magma/trace/out/test_sheep_mating_simultaneous_order_final.log`. The focused
ASan/UBSan runtime passes at
`c/magma/trace/out/test_sheep_mating_simultaneous_order_sanitized.log`, and the
broad aggregate passes in 5:12.02 at 446,248 KB peak RSS, zero major faults,
and zero swap at
`c/magma/trace/out/test_runtime_sheep_mating_simultaneous_order.log`. Two
stopped-oracle scalar guards pass at 4,922 and 4,626 steps/s against the
3,858.9 floor at
`c/magma/trace/out/perf_guard_sheep_mating_simultaneous_order_cpu_1.json` and
`c/magma/trace/out/perf_guard_sheep_mating_simultaneous_order_cpu_2.json`.
The fixed queues allocate nothing, and GPU 1 was untouched.

Two independent same-tick births now match the real dynamic tail exactly. The
strict loot-enabled row dispatches four parents followed by child A, XP A,
child B, and XP B; the no-loot row proves the contiguous child-ID alternative.
Pair B inherits the causal shared Math/EID cursor left by pair A, and both
newborn private RNG states are pinned independently.

The next two cases put a pre-existing XP orb before the parents. Old C's
separate living and XP scans fail immediately by dispatching the parents first
at `c/magma/trace/out/test_sheep_mating_preexisting_xp_old_c.log`. The native
mob runtime now owns a fixed generation-tagged order spanning its represented
living and XP stores. It survives ordinary and controlled tick boundaries,
invalidates expired references, and distinguishes an old orb from a birth orb
that reuses the same XP slot. All fourteen exact cases pass before and after a
cold Java restart at
`c/magma/trace/out/test_sheep_mating_persistent_order_final.log` and
`c/magma/trace/out/test_sheep_mating_persistent_order_restart.log`. The focused
two-tick/slot-reuse gate and sanitizer pass at
`c/magma/trace/out/test_sheep_mating_persistent_order_native.log` and
`c/magma/trace/out/test_sheep_mating_persistent_order_sanitized.log`. The broad
aggregate passes in 6:13.91 at 446,400 KB peak RSS, zero major faults, and zero
swap at `c/magma/trace/out/test_runtime_persistent_order.log`. Two stopped-
oracle scalar guards pass at 4,936 and 5,108 steps/s against the 3,858.9 floor
at `c/magma/trace/out/perf_guard_sheep_mating_persistent_order_cpu_1.json` and
`c/magma/trace/out/perf_guard_sheep_mating_persistent_order_cpu_2.json`. The
list is bounded to 380 references, uses fixed storage, and allocates nothing.

A fifteenth case covers living-slot reuse. A no-AI cow is loaded first at
health zero and death time 19, which is a valid serialized Java death state.
Its update reaches death time 20 and removes it; the later newborn reuses that
native living slot with a new generation. Exact dispatch is cow, parents,
child, breed XP, while final loaded order is parents, child, breed XP. The
matrix passes ten consecutive stress repetitions and after a cold restart;
the final comparison is
`c/magma/trace/out/test_sheep_mating_living_slot_reuse_final.log`. Focused
native and sanitizer gates pass at
`c/magma/trace/out/test_sheep_mating_living_slot_reuse_native.log` and
`c/magma/trace/out/test_sheep_mating_living_slot_reuse_sanitized.log`. The
broad aggregate passes in 5:07.96 at 446,216 KB peak RSS, zero major faults,
and zero swap at `c/magma/trace/out/test_runtime_living_slot_reuse.log`. Two
clean scalar guards pass at 5,033 and 5,174 steps/s at
`c/magma/trace/out/perf_guard_sheep_mating_living_slot_reuse_cpu_1.json` and
`c/magma/trace/out/perf_guard_sheep_mating_living_slot_reuse_cpu_2.json`. The
exact spawn hook now accepts health zero; there is no new tick-path work.

State-capsule import now restores the represented living/XP part of this order
without using EID as a proxy. Java records each captured entity's original
`loadedEntityList` rank before distance sorting; capsule validation requires
complete, unique, non-negative ranks, and magma emits exact NoAI-pig/XP
fixtures in that order. The discriminating capsule contains pig 91 before XP
92 in its distance-sorted payload but restores XP 92 before pig 91. Duplicate,
partial, and ambiguous legacy multi-entity order fail closed. The round-trip
selftest passes at
`c/magma/trace/out/state_capsule_loaded_order.log`. The focused reverse-EID
native case preserves `[92,91]` in both schedulers on tick one and dispatches
the same order on tick two before the XP age-6000 removal; native and sanitizer
evidence is at
`c/magma/trace/out/test_sheep_mating_capsule_order_native.log` and
`c/magma/trace/out/test_sheep_mating_capsule_order_sanitized.log`. The full
15-case Java comparison remains green at
`c/magma/trace/out/test_sheep_mating_capsule_order_final_2.log`. Stopped-oracle
CPU guards pass at 5,131 and 5,134 steps/s at
`c/magma/trace/out/perf_guard_capsule_loaded_order_cpu_1.json` and
`c/magma/trace/out/perf_guard_capsule_loaded_order_cpu_2.json`. The broad
native aggregate passes in 5:07.15 at 446,316 KB peak RSS, zero major faults,
and zero swap at
`c/magma/trace/out/test_runtime_capsule_loaded_order.log`.

Breeding-item feeding is now exact for represented cows, pigs, and chickens as
well as sheep. The shared `EntityAnimal` path recognizes wheat for cows and
sheep; carrot, potato, and beetroot for pigs; and wheat, pumpkin, melon, and
beetroot seeds for chickens. Adult love 600, player credit, status 18,
survival/creative consumption, child age-up/forced-age arithmetic, ordinary
love ticking, damage reset, delayed hand selection, and cross-species rejection
match the real Java interaction. Two final 53-case comparisons pass at
`c/magma/trace/out/test_animal_feed_final_2.log` and
`c/magma/trace/out/test_animal_feed_repeat.log`. Focused native and
AddressSanitizer coverage passes at
`c/magma/trace/out/test_animal_feed_native_final.log` and
`c/magma/trace/out/test_animal_feed_asan.log`. Existing sheep mating remains
green at `c/magma/trace/out/test_animal_feed_sheep_mating_regression.log`.
Stopped-oracle CPU guards pass at 4,928 and 5,045 steps/s at
`c/magma/trace/out/perf_guard_animal_feed_cpu_1.json` and
`c/magma/trace/out/perf_guard_animal_feed_cpu_2.json`. The species-food switch
runs only for an animal entity right-click; ordinary love ticking reuses the
existing passive loop with no new scan or allocation. The broad native
aggregate passes in 5:10.07 at 445,584 KB peak RSS, zero major faults, and zero
swap at `c/magma/trace/out/test_runtime_animal_feed.log`. GPU 1 was untouched.

Cow milking is exact for the represented interaction boundary. Adult survival
cows handle a bucket in either hand, including positive breeding cooldown;
creative players and children pass to the next hand. The player-position
`entity.cow.milk` sound precedes inventory mutation, single-bucket replacement
also emits Forge's resolved `item.armor.equip_generic` sound, and stacked
buckets put one unstackable milk bucket in the first empty main slot. A full
main inventory constructs and tosses the exact milk `EntityItem`, including
four shared Math draws, four player-private float draws, global EID, immediate
pose/motion/hover/yaw, and the runtime item's later same-boundary age/pickup
tick. The strict gates are `c/magma/trace/test_cow_milking.py` and the expanded
`c/magma/trace/test_sheep_feed.py`.

The bounded exact item store rejects an otherwise-valid full-inventory toss
atomically when all 48 item slots are occupied; Java has no such capacity.
Empty-bucket and milk-bucket stack limits in the general shared inventory and
container helpers also remain separately wrong outside this interaction-local
path: Java uses 16 for empty buckets and one for filled/milk buckets.

Pig saddle application and immediate mount association are exact for the
represented interaction boundary.
An adult unsaddled pig handles either hand, sets its synchronized saddle bit,
emits `entity.pig.saddle` at pig coordinates in category `neutral` with volume
0.5 and pitch 1, and consumes one saddle only in survival. Creative applies
the same state and sound without consumption. A child handles the saddle but
does not mutate, sound, consume, or fall through to the other hand. The
integrated runtime applies this on the following server boundary and respects
solid-box occlusion. A plain main-hand name tag is also correctly handled by
`EntityPig.processInteract` and therefore preempts an offhand feed; the old
pass-through behavior was a measured bug. A saddled passenger-free pig mounts
the represented player before a held saddle is reconsidered; eligible food
still feeds first, while cooldown/in-love food falls through to mounting.
Sneaking and an already-saddled child still mount, and a name tag handles
before the mount branch. Association is delayed through the integrated-server
packet boundary, solid-box occlusion applies, and sneak on a later tick clears
the represented association. The expanded 72-case strict gate is
`c/magma/trace/test_sheep_feed.py`.

Pig carrot-on-a-stick steering and boost are exact for the bounded represented
continuation. Either hand enables the client-authoritative ridden branch;
rider yaw/pitch, step and jump factors, base speed, raw position/motion,
double limb update, passenger offset, and the non-steerable client damping
path match. Server item use matches the private-RNG 140..980 duration, sine
curve and post-increment expiry boundary, active-boost rejection, creative
handling, and the ordinary damage-18/19 durability boundary. The strict gate
is `c/magma/trace/test_pig_ride.py`; its original 16 raw-bit Java/native
transitions cover a loaded flat-stone client arena and a separately parked
server item-use boundary. The playable runtime routes main/offhand use through
its delayed server packet boundary and adds no idle scan or allocation.

Persistent dry-land client travel is exact for four bounded 48-tick layouts:
one-block step, two-block wall, two-cell gap, and bottom stone slab. The same
client pig and passenger persist for the complete trace. All 192 rows match
raw pig position, motion, AABB, fall distance, horizontal/vertical collision,
on-ground/alive state, yaw/pitch/render/head/step/jump/AI/limb fields,
passenger pose/motion/ride state, boost state, and private RNG. This extends
the complete ride/dismount/death gate from 32 to 36 strict cases. Ridden pigs
now use the existing state-aware chunk collision collector and exact step
solver; ordinary mobs retain the prior coarse fixed-block path. The active
branch uses fixed scratch storage and no heap allocation. This proves the
tested full-cube and bottom-slab geometry only.

Ridden-pig soul-sand contact is exact for a persistent six-tick trace paired
with an otherwise identical stone-floor control. The pre-fix trace first
diverged at tick 1 only in raw horizontal motion. The active ridden path now
collects bounded soul-sand cells from the cached chunk window and applies the
Java 0.4 horizontal-motion multiplier once per overlapping cell, after
collision resolution and before final friction. Position, vertical motion,
geometry/contact state, passenger state, boost state, and private RNG remain
exact throughout. These two rows extend the complete gate from 36 to 38
strict cases; two consecutive full comparisons pass. The implementation uses
fixed trailing contact-cell scratch, no heap allocation, and no idle scan.
The broad native aggregate passes in 5:33.74 at 445,864 KB peak RSS with zero
major faults and zero swap; the stopped-oracle scalar guard passes at 4,780
steps/s against the 3,858.9 floor in
`c/magma/trace/out/perf_guard_pig_soul_sand_cpu.json`. This does not promote
fluid, ladder, slime, or other block-contact callback behavior.

The persistent ridden-pig web latch is exact for a six-tick cobweb corridor.
The first move through an overlapping web is unscaled; the final contracted
AABB sets `isInWeb` and resets fall distance. The following move consumes the
latch with exact 0.25 X/Z and 0.05000000074505806 Y displacement factors,
zeros stored motion, and relatches while overlap continues. The strict trace
compares the latch explicitly alongside raw pig/passenger movement, AABB,
fall/contact state, pose/limbs, boost state, and private RNG. After adding the
callback and persistent latch, the discriminator failed first at tick 1
because the exact-AABB move wrapper bypassed the existing web consumer;
position, horizontal motion, passenger position, and limbs differed while the
latch matched. The wrapper now consumes it at Java's move boundary.

Web and soul sand share one fixed tagged contact list in x/y/z callback order,
one active-ridden scan, no heap allocation, and no idle-path work. This grows
the complete gate from 38 to 39 strict cases; two full comparisons pass in
19.73 and 20.00 seconds at 30,252 KB harness RSS. The broad native aggregate
passes in 5:18.13 at 445,732 KB peak RSS with zero major faults and zero swap;
the stopped-oracle scalar guard passes at 4,815 steps/s against the 3,858.9
floor in `c/magma/trace/out/perf_guard_pig_web_cpu.json`. Water,
slime, and remaining block-contact callback behavior are still open.

Bounded ridden-pig north-ladder contact and forward climb are exact through
paired six-tick clear and two-high ladder traces. The fixture begins in a valid
non-overlapping state 0.0125 blocks short of the ladder plane. Before the fix,
the ladder row first differed at tick 0 only in vertical motion: Java applies
the living-entity ladder clamps before movement, then a 0.2 upward impulse
after horizontal collision and before gravity/drag. Position, AABB, collision,
ladder state, passenger, pose/limbs, boost, and private RNG already matched.
The active-ridden path now finds the exact feet cell in the existing tagged
contact list, applies the pre-move clamps/fall reset and the post-collision
climb impulse at those measured boundaries, with no new scratch, allocation,
idle scan, or ordinary-mob work.

This grows the complete gate from 39 to 41 strict cases; two full comparisons
pass in 21.51 and 22.11 seconds at 30,252 KB harness RSS. JDK 8, product,
focused mob/player, CPU physics/entity-spine, CUDA compile-only entity-spine,
and nine-layout all-source AddressSanitizer gates pass. The broad native
aggregate passes in 5:39.28 at 445,380 KB peak RSS with zero major faults and
zero swap; the stopped-oracle scalar guard passes at 4,693 steps/s against the
3,858.9 floor in `c/magma/trace/out/perf_guard_pig_ladder_cpu.json`. Other
ladder facings, vines, trapdoor ladders, side/top/falling contacts, clamp-limit
cases, water, slime, and remaining callbacks are still open.

Bounded non-sneaking ridden-pig slime landing and bounce are exact through
paired six-tick stone/slime traces. Both begin 0.5 blocks above the support
surface, airborne, with vertical motion -0.6. Before the fix, the stone control
was exact and the slime row first differed at tick 0 only in vertical motion:
Java retained `3fdfdc9c5810624e`, while magma produced
`bfb41205c28f5c29`. Java's living slime callback negates the pre-collision
negative motion after AABB resolution and before gravity/drag. The
active-ridden path now preserves that value, selects the exact block 0.2 below
the final feet position from the existing tagged contact list, and applies the
callback at the measured boundary. Slime's 0.8 slipperiness is also selected
on the following grounded boundary. This adds one contact tag, no state,
scratch, allocation, extra scan, or idle work.

This grows the complete gate from 41 to 43 strict cases; two full comparisons
pass in 21.77 and 22.19 seconds at 30,252 KB harness RSS. JDK 8, product,
focused mob/player, CPU physics/entity-spine, CUDA compile-only entity-spine,
and eleven-layout all-source AddressSanitizer gates pass. The broad native
aggregate passes in 5:34.91 at 445,960 KB peak RSS with zero major faults and
zero swap; the stopped-oracle scalar guard passes at 5,004 steps/s against the
3,858.9 floor in `c/magma/trace/out/perf_guard_pig_slime_cpu.json`. Slime
sneaking/nonliving variants, mixed web/slime contact, water, and remaining
callbacks are still open.

Bounded low-speed ridden-pig slime walking damping is exact through paired
six-tick stone/slime traces starting 0.01 blocks above the surface, airborne,
with vertical motion -0.05. With bounce already exact, the stone control
passed and the slime row first differed at tick 1 only in horizontal motion:
Java Z was `3f7cb9da7117f2aa`, while magma retained `3f91b0be16e1b080`;
vertical motion matched at `bfa933c36e171b09`. The landing helper now applies
Java's `0.4 + abs(post-onLanded motionY) * 0.2` X/Z multiplier when the final
body is grounded and vertical speed is below 0.1, before generic contacts,
gravity, and friction. This adds only arithmetic after the already-selected
slime contact, with no new state, scratch, scan, allocation, or idle work.

This grows the complete gate from 43 to 45 strict cases; two full comparisons
pass in 22.61 and 23.01 seconds at no more than 30,252 KB harness RSS. JDK 8,
product, focused mob/player, CPU physics/entity-spine, CUDA compile-only
entity-spine, and thirteen-layout all-source AddressSanitizer gates pass. The
broad native aggregate passes in 5:36.20 at 446,292 KB peak RSS with zero major
faults and zero swap; the stopped-oracle scalar guard passes at 4,869 steps/s
against the 3,858.9 floor in
`c/magma/trace/out/perf_guard_pig_slime_walk_cpu.json`. Slime
sneaking/nonliving and mixed-contact variants, water, and remaining callbacks
are still open.

Bounded client-controlled ridden-pig travel while continuously immersed in
still source water is exact for six ticks. The trace now exposes `isInWater`
alongside raw pig/passenger movement, AABB, fall/collision state, pose/limbs,
boost, and private RNG. Before the fix, tick 0 took the dry branch: Java
motion Y/Z were `bf947ae147ae147b` / `3f90624dd0e56040`, while magma produced
`bfb41205c28f5c29` / `3f9f7318c2ca56c0` and reported `is_in_water=false`.
The active mounted path now reuses the exact material/current probe and
applies the 1.11.2 water moveRelative, exact AABB move/callbacks, 0.8 drag,
0.02 gravity, and horizontal edge-climb ordering. A dry control at a different
fixture coordinate also exposed one-ULP loss from reconstructing the AABB
from center position each tick; the mounted path now retains the exact swept
box.

The complete gate grows from 45 to 46 strict cases and passes twice in 24.80
and 24.68 seconds at 30,252 KB harness RSS. JDK 8, product, focused mob/player,
CPU physics/entity/player, CUDA compile-only entity/player, and fourteen-
layout all-source AddressSanitizer gates pass. The broad native aggregate
passes in 5:38.36 at 446,104 KB peak RSS with zero major faults and zero swap;
the stopped-oracle scalar guard passes at 4,753 steps/s against the 3,858.9
floor in `c/magma/trace/out/perf_guard_pig_water_cpu.json`. The bounded probe
runs only for the active ridden pig and has no heap allocation or global scan.
Dry-to-still-water entry is also exact for a bounded four-tick ridden-pig
trace. The pig begins 0.05 blocks short of a single source. Java performs dry
travel on tick 0, then its in-move `EntityLivingBase.updateFallState` water
probe sets `isInWater`, resets fall distance, and invokes `Entity.resetHeight`.
That method consumes 97 pig-private random draws for its sound pitch, bubble,
and splash construction. Native now performs the same post-move probe and
width-dependent RNG advancement; before the fix its tick-0 cursor was
27,500,032,739,863 rather than Java's 75,376,161,696,822. The 47-case strict
matrix passes twice, and the broad native and scalar performance gates remain
clean.

The entry-current ordering is exact for one perpendicular gradient: a level-0
source with an adjacent level-1 cell produces a pure +X push. Java applies the
0.014 current after the dry AABB move but before callbacks, gravity, and land
drag. The old post-tick native hook instead produced motion X
`3f8cac083126e979` against Java's `3f7f4f50dd2f1aa0`. The active steerable
dry-pig path now splits AABB movement at `EntityLivingBase.updateFallState`,
applies the material/current probe, delegates the resolved displacement to
base fall bookkeeping, and resumes callbacks plus the land tail in order. A
separate truly dry falling entry starts at Y=221 with motion Y=-1.0 and proves
that water resets prior fall distance before the resolved 0.98-block descent
is stored as `3f7ae148`. The 49-case strict matrix passes twice; corrected
seventeen-layout sanitizer, broad-native, and scalar performance gates pass.
Broader gradients, falling-water metadata/downward currents, horizontal
edge-climb fixture coverage, lava, emitted splash/sound events, swim AI, and
broader living entities remain open.

Horizontal ridden-water edge climb is now exact for a discriminating positive
and blocked control. Both one-tick layouts collide with the same wall while
off ground. When the destination probe is dry, Java and native replace motion
Y with `3fd3333340000000` (`0.30000001192092896`); when that probe contains
water, both retain `3fdcd35a8d0e5603`. The earlier grounded candidate was
discarded because step-height retry bypassed the collision and never reached
the branch. The complete strict matrix is now 51 cases and passes twice, with
nineteen-layout sanitizer, JDK 8, product, focused mob, broad-native, and
scalar performance gates passing. The full aggregate takes 6:43.01 at
675,952 KB peak RSS with zero swap, and the scalar result is 5,076 steps/s
against the 3,858.9 floor.

The aggregate also repaired several stale boundaries rather than weakening
them. Exact EntityItem ID zero is now accepted, matching Java's first static
entity ID and a new narrow native assertion. Runtime test scripts that link
`runtime.o` now link `nbt_blob.o`; sugar cane has adjacent water; boat pressure
plates have support; end-to-end combat emits click edges and waits for the
delayed server packet plus hurt resistance; crystal attacks use pillar cover;
and the End bed uses natural recovery plus a carried blast-cover block. The
fresh-spawn-through-credits route passes under these survival constraints.
Broader current gradients, falling-water metadata/downward currents, emitted
splash/sound events, swim AI, and broader living entities remain open.

Bounded ridden-pig lava travel is exact for still immersion, dry entry, and
the positive/blocked horizontal edge-climb branch. Lava uses Java's 0.02F
acceleration, collision/callback order, 0.5 drag on all axes, 0.02 gravity,
and exact `0.30000001192092896` climb replacement. A mixed control spans water
and lava simultaneously and proves that water selects the travel branch while
both raw material predicates remain true.

Authoritative lava contact is separately exact because the integrated client
and server hold distinct pig copies. A real mounted `EntityPlayerMP` cannot
steer the parked server pig, so the contact oracle deliberately measures the
server base tick while the client trace measures vehicle travel. Dry and
sustained 12-tick rows match health, fire, fall distance, hurt timers, last
damage, alive state, pig-private RNG, and Math RNG. Native therefore retains
separate server fall/contact/RNG state; the server hurt-sound draws do not
advance the verified client movement cursor. The 58-case matrix passes twice,
the 26-layout all-source sanitizer passes, the 6:40.42 broad aggregate passes
at 675,992 KB peak RSS, and scalar throughput remains 5,076 steps/s against
the 3,858.9 floor. Flowing-lava metadata, contact death/drop, general mob
potion/effect storage, emitted effects, swim AI, and broader living entities
remain open.

The isolated authoritative ridden-pig vehicle-packet cactus and fire boundary
is now exact. The oracle invokes the real
`NetHandlerPlayServer.processVehicleMove` with a mounted server pig and then
runs that pig's matching ordinary base tick. The vehicle remains unspawned in
the isolated world list, which prevents client passenger synchronization from
injecting extra vehicle packets, while the actual riding association still
makes the server accept the packet. A server-action-local Math cursor prevents
the integrated client thread from contaminating the measured global RNG.
Twelve-tick dry, cactus, fire, and combined cactus-plus-fire rows match health,
fire, fall distance, water/lava predicates, hurt timers, last damage, alive
state, pig-private RNG, and Math RNG. The combined side-by-side layout proves
that cactus is the sole accepted fresh hit and RNG consumer while rejected
in-fire damage still starts the fire counter. A separate two-tick wet row
establishes actual still water, applies a 100-tick burning precondition, and
proves the packet move's two-float extinguish-sound draw before the ordinary
base tick normalizes fire to zero without ON_FIRE damage. Native exposes the
same cold action boundary and retains a separate server water predicate: the
accepted grounded packet clears only the server fall ledger, cactus callback
damage precedes generic in-fire handling and wet cleanup, and the base tick
runs water before fire before lava. The added fixed-window water probe runs
only for the actively ridden server pig, with no allocation, global scan,
ordinary-mob work, or idle work. The 63-case matrix passes twice, the 31-layout
all-source sanitizer passes, the broad aggregate passes in 6:55.31 at 675,992
KB peak RSS, and scalar throughput is 5,141 steps/s against the 3,858.9 floor.
That cold boundary did not by itself imply automatic playable block-contact
dispatch. The represented shared-pose integration described below now closes
that narrow gap; general moving vehicle deltas, contact death/drop, emitted
effects, and broader living entities remain open.

Fire Resistance is now exact at this represented mounted-pig server boundary.
The Java fixture applies the real effect with hidden particles and measures its
remaining duration. A two-tick packet-fire row blocks both IN_FIRE and
same-tick ON_FIRE before expiring, then accepts the first ordinary post-expiry
hit; a one-tick lava row blocks damage while retaining the 300-tick fire floor
and fall-distance halving; a combined cactus/fire row proves cactus remains
unblocked. Native stores only the authoritative duration for the active ridden
pig, uses the shared exact combat predicate before hurt arbitration and RNG,
and decrements after the current fire/lava phase. The 66-case matrix passes
twice, the 34-layout all-source sanitizer passes, the broad aggregate passes in
6:51.39 at 675,956 KB peak RSS, and scalar throughput is 5,126 steps/s against
the 3,858.9 floor. General mob potion storage/effect routing, moving
vehicle-packet reconciliation, contact death/drop, and emitted effects remain
open.

Packet-stage lava is now exact and separately observable from the following
base tick. Every packet trace records an immediate post-vehicle-move state and
a post-base-update state. This matters because lava is flammable during
`Entity.move`: the packet first applies IN_FIRE and raises fire to 160, then
the base phase runs ON_FIRE followed by LAVA and raises fire to 300. Without
the intermediate state, the final health and RNG can coincide with a direct
base-only lava hit. The normal row proves the packet's health 9/lastDamage 1
state before the base finishes at health 6/lastDamage 4, and its grounded move
keeps server fall distance at zero. A Fire Resistance expiry row locks the
rejected damage but preserved ignition/fall effects before normal damage
resumes. Native feeds both fire and lava through the same cold packet
flammable-contact branch, adding no state or runtime scan. The 68-case matrix
passes twice, the 36-layout all-source sanitizer passes, and scalar throughput
is 5,088 steps/s against the 3,858.9 floor. General moving vehicle-packet
reconciliation, mob effect storage, contact death/drop, and emitted effects
remain open.

Automatic represented shared-pose vehicle-packet contact dispatch is now
integrated into the playable runtime. After active carrot-stick pig travel,
the client side queues the mounted EID. The server consumes that packet after
the ordinary player packet and before player timers, hazards, or the
controlled-living update, matching the Java handler boundary. It derives
cactus from the contracted callback AABB, fire/lava from the contracted
flammability AABB, and wet state from the authoritative server contact flag,
then reuses the verified packet-contact transition. The pending branch has no
allocation or entity scan; its bounded block walk runs only for an active
packet.

Nine integration rows reach this path exclusively through `gm_runtime_tick`
and compare an automatic checkpoint in addition to the packet and post-base
states. They extend the complete Java/native matrix to 77 cases, passing twice
in 36.15 and 36.29 seconds at 30,252 KB harness RSS. Raw position and AABB
observability exposed an earlier fixture defect: the native authoritative pig
had been allowed to steer 0.05625 blocks on tick zero even though a real
`EntityPlayerMP` passenger cannot steer. The measured server fixture now
removes the carrot stick before the base tick, and both engines remain
stationary. The all-source sanitizer passes the same 77 cases in 139.48
seconds at 100,880 KB. Product, focused mob, and broad game gates pass; the
broad gate takes 488.59 seconds at 676,552 KB peak RSS. With the oracle
stopped, scalar throughput is 5,061 steps/s against the 4,062 baseline and
3,858.9 floor. GPU 1 was untouched.

The direct dry moving-packet boundary is now exact for three discriminating
cases. A same-height horizontal packet is accepted in open space, a one-block
move into a two-high stone wall takes Java's wrong-move/collision rollback,
and an eleven-block move takes the greater-than-100 speed rejection. Both
hidden `lowestRidden` tracker triplets are exposed immediately after the real
Java handler. Native applies the `-1e-6` vertical move, 0.0625 contracted old
and target collision queries, X/Z residual threshold, incoming-rotation
rollback rule, old-rotation speed correction, and accepted tracker advance.
The collision collector's broadphase candidates are exact-filtered before the
clearance predicates, which fixed a false wall result caused by floor cells in
the candidate set.

These three rows extend the complete Java/native matrix to 80 cases, passing
twice in 38.62 and 38.60 seconds at 30,252 KB harness RSS. The all-source
AddressSanitizer and LeakSanitizer run passes the same 80 cases in 151.36
seconds at 100,900 KB. Product, focused mob, and broad game gates pass; the
broad gate takes 633.66 seconds at 676,164 KB while the Java oracle is also
using about eight CPU cores. With the oracle stopped, scalar throughput is
5,140 steps/s against the 4,062 baseline and 3,858.9 floor. GPU 1 was
untouched.

The playable runtime queue now carries the full client target pose and retains
an independent authoritative pig body. A real client tick emits the represented
nonzero dry target, the following runtime tick consumes it without changing the
client EwStore or AABB at the immediate packet checkpoint, and explicitly
injected wall and speed rows expose both correction branches. This is a
compositional native-runtime/real-Java-handler proof, not a Java integrated
client-to-server trace. The three runtime rows extend the complete matrix to 83
cases, passing twice in 44.17 and 43.22 seconds; a clean-oracle all-source
sanitizer run passes in 159.34 seconds at 102,288 KB. Product, focused mob, and
broad game gates pass, with the final broad gate at 417.74 seconds and 675,980
KB. Stopped-oracle throughput is 5,132 steps/s against the 3,858.9 floor.

The bounded dry post-packet server base state is now exact on that independent
body. Runtime retains the steering item while the non-local server passenger
aligns pig rotation and zeros motion, probes liquids from the server AABB, uses
server coordinates for environment events, and preserves the established
shared timer and server-RNG ordering. The three runtime rows now compare both
the immediate handler checkpoint and Java's post-base state, including raw
pose/AABB, motion, rotation, fall/on-ground state, trackers, combat/effect
state, and RNG cursors. The zero-delta contact transition also clears the
independent server fall ledger. The unchanged 83-case matrix passes twice in
43.64 and 44.49 seconds at 30,252 KB; a clean all-source ASan/UBSan/LSan run
passes in 176.78 seconds at 124,084 KB. The broad gate passes in 414.01 seconds
at 676,440 KB, and stopped-oracle throughput is 5,182 steps/s against the
3,858.9 floor. GPU 1 was untouched.

First-packet moving contacts are now promoted at `Entity.move`'s resolved
temporary server AABB before the handler decides collision rollback. Accepted
fire and lava moves, a cactus contact whose damage survives XYZ rollback, and
wall-beyond-fire/cactus negative controls prove that callbacks use neither the
raw target nor the final restored pose. `EntityLivingBase.updateFallState`'s
asymmetric water rule is also exact: a previously dry pig refreshes water entry
during the move, while a previously wet pig remains wet through a move into
fire until the following base tick. The transition is allocation-free and
reuses the fixed collision scratch and local chunk window.

The fresh-pig zero-draw `firstUpdate` control now composes with four persistent
packet-chain rows. Within one `NetHandlerPlayServer` epoch, both packets keep
the original `lowestRidden` speed baseline while an accepted first packet
advances only `lowestRidden1`; the ordinary server/network tick then re-seeds
both triplets from the authoritative pose. A 10.1-block absolute second target
would exceed the speed threshold from the stale mount origin but remains below
it from the correctly re-seeded first target. Native performs that re-seed at
the represented server base boundary and does not allocate or scan idle mobs.

`Entity.firstUpdate` is construction lifecycle state, not mount lifecycle
state. The server vehicle shadow now inherits it from the pig's represented
tick age and clears it only after the authoritative base phase. A later mounted
entry and a pre-ticked saved-state mount both consume the adult pig's exact 97
private-RNG float draws during the packet's virtual
`EntityLivingBase.updateFallState`: two sound-pitch draws, 19 three-draw bubble
iterations, and 19 two-draw splash iterations. The measured post-packet seed is
exactly Java LCG^97 of the pre-packet seed, while Math RNG is unchanged.
Broader authoritative base ticks with complete previous/render/limb
bookkeeping, long mixed-axis and repeated-correction packet bursts, and broader
network scheduling remain the next packet-layer boundary.

The complete 90-case Java/native matrix passes twice in 47.24 and 47.76
seconds at 30,252 KB RSS. The all-source ASan/UBSan/LSan matrix passes in
201.10 seconds at 124,100 KB after the documented packet-lava oracle flake was
discarded and the complete run repeated. JDK 8, product, focused mob, and broad
fresh-spawn-through-credits gates pass; the broad gate takes 415.64 seconds at
676,204 KB with zero major faults and zero swap. Stopped-oracle scalar
throughput is 4,951 steps/s against the 3,858.9 floor. GPU 1 was untouched.

The expanded 94-case matrix passes in 53.98 seconds at 30,252 KB after the
preceding 93-case matrix passed twice in 49.14 and 49.07 seconds. A clean-oracle
all-source ASan/UBSan/LSan run passes all 94 cases in 201.61 seconds at 119,344
KB. JDK 8, product, focused mob, and broad game gates pass; the broad aggregate
takes 408.88 seconds at 675,984 KB with zero major faults and zero swap.
Stopped-oracle throughput is 5,049 steps/s against the 3,858.9 floor in
`trace/out/perf_guard_pig_packet_chain_cpu.json`. GPU 1 was untouched.

Bounded vertical vehicle packets now pass through both the direct
authoritative seam and the integrated dual-pose runtime. The exact matrix
covers an accepted 0.25-block ascent, downward floor-target correction, and
ceiling-target correction after temporary movement. The vertical residual is
intentionally excluded from the wrong-movement norm because Java 1.11.2's
finite-Y residual branch always clears it; target-box collision still governs
rollback. A two-packet same-epoch ascent proves `lowestRiddenY1` advances after
acceptance while the primary Y speed baseline remains at the epoch origin.
The complete 101-case Java/native matrix passes twice in 54.84 and 54.75
seconds at 30,252 KB. An all-source ASan/UBSan/LSan run passes all 101 cases in
250.99 seconds at 124,340 KB. JDK 8, product, focused mob, and broad game gates
pass; the broad aggregate takes 429.19 seconds at 675,996 KB with zero major
faults and zero swap. Stopped-oracle throughput is 4,961 steps/s against the
3,858.9 floor in `trace/out/perf_guard_pig_vertical_packet_cpu.json`. GPU 1 was
untouched.

One higher-count same-epoch tracker discriminator is now exact. Four packets
without an intervening server base step produce accept, collision rollback,
speed rollback, then accept. The original `lowestRidden` triplet stays fixed
through the burst, the secondary triplet advances only on each accepted
packet, and both correction rotations match Java's distinct branches.

Bounded correction delivery and client application are also exact. A real
`NetHandlerPlayClient.handleMoveVehicle` fixture captures the callback and
swallows only its outbound acknowledgement before it reaches the parked
integrated server. Both collision-rotation and speed-rotation variants prove
that Java replaces only the lowest ridden pig's current pose and AABB,
preserves its motion and on-ground flag, leaves the passenger at the predicted
position until ordinary ridden update, and immediately returns an exact-pose
`CPacketVehicleMove`. Magma applies the same callback to its client pig and
queues the acknowledgement separately from the later predicted packet in a
fixed two-entry FIFO. A native runtime assertion proves both packets survive
the correction tick and drain in order at the next represented server epoch.
No allocation or idle mob scan was added.

The complete 104-case Java/native matrix passes twice in 55.45 and 55.83
seconds at 30,252 KB. One intervening long-session dry-contact fixture failure
passed immediately in isolation and on a clean restarted oracle. The
all-source ASan/UBSan/LSan matrix passes all 104 cases in 242.12 seconds at
123,988 KB. JDK 8, product, focused mob, and broad game gates pass; the broad
fresh-spawn-through-credits aggregate takes 431.72 seconds at 675,992 KB with
zero major faults and zero swap. Stopped-oracle throughput is 4,909 steps/s
against the 3,858.9 floor in
`trace/out/perf_guard_pig_correction_delivery_cpu.json`. GPU 1 was untouched.
Long mixed-axis and repeated-correction bursts, broader packet scheduling, and
complete authoritative previous/render/limb bookkeeping remain open.

A third complete matrix in one long-lived oracle session once caused the Java
packet-lava Fire Resistance row to skip its vehicle move. Product and ASan
binaries both passed that row in isolation, and the complete clean-session
ASan run passed all 83 cases. Acceptance therefore uses the clean-session
result; deterministic reset of every integrated-server packet-fixture side
effect across repeated whole matrices remains a harness issue.

Explicit server pig dismount placement is exact for the bounded represented
full-stone, water, and isolated lower-support layouts. The implementation
follows the fixed nine-probe Java order, including its repeated final
candidate, rotates that order from the pig's facing, distinguishes
`isSideSolid(UP)` support from water fallback, and uses the exact
float-widened rider and pig dimensions. Candidate clearance reuses the
state-aware player collision-shape collector over the runtime's already
cached chunk window; no new allocation or world scan is added. Flat yaw 0/90,
first-cell obstruction at both facings, total obstruction through the
no-epsilon pig-top fallback, and isolated stone, top slab, bottom slab,
eight-layer snow, seven-layer snow, and water lower cells all match raw
player/pig positions and AABBs. The top slab and eight-layer snow support the
first candidate; the bottom slab and seven-layer snow correctly fall back to
the pig top. Motion, look, on-ground, fall distance, EIDs, and every RNG cursor
remain unchanged. These twelve rows extend
`c/magma/trace/test_pig_ride.py` to 32 strict Java/native transitions after
the four terminal rows. The playable sneak and terminal death routes call the
same action-only helper.

Ridden pig terminal death now uses that same placement rule at the exact
world-update boundary. Ordinary product pig death keeps the loaded pig,
saddle, and passenger association through `deathTime` 0..19, rejects repeat
damage/drop emission, and moves terminal XP, particles, dismount, and slot
retirement to time 20. A valid health-zero/time-19 save-state fixture calls a
real `WorldServer.updateEntities`; Java visits the pig, sets `Entity.isDead`,
emits its terminal particles, recursively updates the still-associated player,
then removes the pig. Native matches that pig-then-passenger order, raw player
position/AABB/motion/look/contact/fall state, preserved post-removal pig data,
private particle RNG, and shared cursors for flat and all-blocked layouts at
yaw 0/90. These four rows extend `c/magma/trace/test_pig_ride.py` to 26 strict
transitions. Direct explicit dismount preserves fall distance; the terminal
`EntityLivingBase.updateRidden` boundary resets it to zero, as in Java.

The ordinary saddled-pig death drop is exact. Vanilla emits the base pig pork
first, then appends one saddle from `EntityPig.onDeath`; unlike pork, the
saddle is independent of `doMobLoot`. The five-case strict gate at
`c/magma/trace/test_pig_death.py` covers saddled and unsaddled pigs,
`doMobLoot` on and off, raw and cooked pork, ordered item insertion, exact
item constructor state, global EIDs, and all private/shared RNG cursors. A
normal product-path death regression also proves pork-then-saddle order, the
20-tick retained passenger window, and terminal rider placement. As with other
exact entities, the bounded native store rejects
the transition atomically if it lacks capacity for every resulting item;
Java's entity list has no corresponding fixed capacity.

The ordinary player-melee lethal pig boundary is now exact from the accepted
hit through `deathTime` 19. A real normal-AI, moving, saddled, mounted Java pig
runs `attackEntityFrom(causePlayerDamage(...))`; magma matches status 2,
set-been-attacked and knockback RNG, knockback motion, death sound and pitch,
health/hurt/recent-hit/player-credit state, raw-versus-cooked pork count,
`doMobLoot`, pork-before-saddle construction, status 3, item IDs/pose/motion,
and every private/shared RNG cursor. The dying pig then matches gravity,
collision, friction, raw pose/AABB, passenger following, player AABB/motion and
fall-distance reset, item physics, and pig-player-item update order at ticks 1
and 19. The four-case strict gate is
`c/magma/trace/test_pig_lethal.py`; it covers raw, cooked, no-loot, yaw 0/90,
and independent seeds.

The Java fixture cancels normal server drop spawning and reinserts the real
captured `EntityItem`s without client-mirror packets. This is required because
the integrated client shares process-global `Entity.nextEntityID` and
`Math.random`; delayed mirror constructors otherwise make a repeated matrix
case-order dependent. Five repeated legacy death matrices and three repeated
lethal matrices pass after this isolation fix.

Broader shaped dismount layouts beyond the promoted slab/snow/water lower
supports, dynamic moving-block collision, ridden fluid/web/ladder/slime and
block-contact behavior, server correction/packet timing, client data-manager
notification/render state,
Unbreaking or arbitrary carrot-stick NBT, saddle rendering, custom named-tag
mutation, and entity NBT persistence remain open. The source's raised-support
branch is not reachable for the normal seated standard player with the tested
static vanilla UP-solid blocks because those blocks first collide with the
candidate box; dynamic or nonstandard geometry remains unpromoted. Non-player
ordinary pig damage routes that still
enter the coarse `mob_drop` constructor retain their pre-existing `doMobLoot`,
count/cooking, item-pose, and global-EID limitations; source-specific hit
ordering outside player melee also remains unpromoted. Accepted lower
animal tasks, obstacle/gap and persistent multi-tick navigation, and
multi-entity/global item ordering around animal item creation also remain open.

The immediate direct `EntityAIMate` birth boundary is now exact for cows,
pigs, and chickens as well as sheep. The generic callback requires identical
runtime species, reaches birth only on update 60 with squared distance below
9, consumes the child EID followed by exactly three `Math.random` doubles,
leaves World RNG untouched outside sheep genetics, resets parent love and age,
spawns the same-species age-24000 child at the initiator pose, emits seven
species-sized hearts from the initiator private RNG, and conditionally appends
the same 1..7 XP orb. Two 23-case Java comparisons pass at
`c/magma/trace/out/test_animal_mating_final.log` and
`c/magma/trace/out/test_animal_mating_final_repeat.log`. The native component
also rejects mixed-species parents and preserves the sheep compatibility API;
the focused component passes in 0.16 seconds at 63,984 KB peak RSS at
`c/magma/trace/out/test_animal_mating_native_final.log`, and AddressSanitizer
passes at `c/magma/trace/out/test_animal_mating_asan.log`. The broad native
aggregate passes in 5:28.64 at 446,096 KB peak RSS, zero major faults, and zero
swap at `c/magma/trace/out/test_runtime_animal_mating_direct.log` and `.time`.
Stopped-oracle CPU guards pass at 5,166 and 5,129 steps/s at
`c/magma/trace/out/perf_guard_animal_mating_direct_cpu_1.json` and
`c/magma/trace/out/perf_guard_animal_mating_direct_cpu_2.json`.

The direct boundary now also transports the otherwise hidden newborn state.
Java constructs every child with a fresh private `Random`; the fixture pins a
post-construction 48-bit cursor plus its Gaussian cache, while chicken's actual
constructor-derived `timeUntilNextEgg` in 6000..11999 is captured dynamically.
Native consumes those fields from one bounded species-neutral FIFO before the
child becomes observable. Two repeated 23-case comparisons pass at
`c/magma/trace/out/test_animal_child_state_final_1.log` and
`c/magma/trace/out/test_animal_child_state_final_2.log`. The focused runtime
passes in 0.15 seconds at 63,984 KB peak RSS, and the all-source AddressSanitizer
run passes in 0.48 seconds at 110,252 KB peak RSS at
`c/magma/trace/out/test_animal_child_state_native_final.log` and
`c/magma/trace/out/test_animal_child_state_asan.log`. Stopped-oracle CPU guards
pass at 5,037 and 5,033 steps/s against the 3,858.9 floor. The broad native
aggregate passes in 5:04.33 at 446,556 KB peak RSS, zero major faults, and zero
swap at `c/magma/trace/out/test_runtime_animal_child_state.log` and `.time`.
The fixed state adds 768 bytes and no allocation or idle scan.

Cow, pig, and chicken now enter the ordinary live mating scheduler rather than
only the direct callback. The species-neutral task path selects the nearest
same-species loving mate, restores the update-60 boundary, appends child then
optional XP to persistent order, and dispatches the newborn later in the same
world boundary. Chicken construction initializes its exact 6000..11999 timer
from the post-UUID entity RNG, and its post-`EntityAnimal` tail now preserves
the five flap floats, jockey flag, post-move fall damping, adult timer
decrement, and child timer short circuit. The fixture transports the real
constructor-derived newborn timer before comparing C.

The final strict matrix retains all 15 mature sheep/order cases and adds one
airborne cow, one airborne pig, and airborne plus stationary-grounded chicken
births. Two 19-case runs pass at
`c/magma/trace/out/test_animal_live_scheduler_final_1.log` and
`c/magma/trace/out/test_animal_live_scheduler_final_2.log`; the 23-case direct
matrix remains green at
`c/magma/trace/out/test_animal_live_scheduler_direct_regression.log`. Focused
native and ASan/UBSan gates pass at
`c/magma/trace/out/test_animal_live_scheduler_native.log` and
`c/magma/trace/out/test_animal_live_scheduler_asan.log`. Two stopped-oracle
CPU guards pass at 5,108 and 5,079 steps/s against the 3,858.9 floor at
`c/magma/trace/out/perf_guard_animal_live_scheduler_cpu_1.json` and
`c/magma/trace/out/perf_guard_animal_live_scheduler_cpu_2.json`. The broad
native aggregate passes in 5:34.22 at 446,192 KB peak RSS, zero major faults,
and zero swap at
`c/magma/trace/out/test_runtime_animal_live_scheduler.log` and `.time`.

The first unobstructed grounded mating step is now exact for every represented
animal. The mating-only MoveHelper uses Java's per-species movement attribute
for both `moveForward` and `landMovementFactor`: sheep 0.23, cow 0.20, and
pig/chicken 0.25. Cow axial, pig diagonal, and chicken 90-degree-clamped cases
exercise the existing PathPoint center, table-based atan2, angle limit, travel,
collision push, birth, and same-boundary child/XP continuation. Two repeated
23-case comparisons pass in 1.38 and 1.40 seconds at
`c/magma/trace/out/test_animal_grounded_geometry_final_1.log` and
`c/magma/trace/out/test_animal_grounded_geometry_final_2.log`. The exact speed
branch is active-mating-only, with no allocation, scan, or CUDA change.
ASan/UBSan passes at
`c/magma/trace/out/test_animal_grounded_speed_asan_final.log`; the broad native
aggregate passes in 5:32.58 at 446,068 KB peak RSS, zero major faults, and zero
swap at `c/magma/trace/out/test_runtime_animal_grounded_speed.log`. With Java
stopped, scalar guards pass at 5,038 and 5,164 steps/s against the 3,858.9
floor. Obstacle/gap paths, persistent multi-tick navigator/helper state, and
accepted lower tasks remain open.

The isolated adult chicken timer-expiry boundary is exact: Java's sound event,
two private-RNG pitch floats, global EID allocation, four `Math.random`
constructor values, timer reset, and the appended egg item's same-boundary
age/pickup/physics tick all match. One focused and two repeated full 20-case
Java comparisons pass at
`c/magma/trace/out/test_chicken_egg_threshold.log`,
`c/magma/trace/out/test_animal_live_egg_full_1.log`, and
`c/magma/trace/out/test_animal_live_egg_full_2.log`. Native full-item-capacity
coverage deliberately drops the unrepresentable item and increments
`spawn_fail_count` while still preserving Java's sound, RNG, EID, and timer
side effects. ASan/UBSan and the broad runtime aggregate pass at
`c/magma/trace/out/test_chicken_egg_asan.log` and
`c/magma/trace/out/test_runtime_chicken_egg.log`; the latter takes 5:28.18 at
446,428 KB peak RSS, zero major faults, and zero swap. With Java stopped,
scalar guards pass at 5,115 and 4,952 steps/s against the 3,858.9 floor.
Save/reload treatment of non-persistent `Entity.rand`, multi-chicken and global
cross-store ordering, accepted tempt/follow/wander/watch goals, and
statistics/criteria remain open.

This is not yet a global Java `loadedEntityList`. Items, TNT, projectiles,
falling blocks, crystals, dragons, players, teams, and riding relations remain
in independent runtime stores, so their cross-store interleavings are still
case-specific. Older multi-restorable-entity capsules must be regenerated
because they never captured Java list order. Clock-dependent construction
without a restored RNG state, entity cramming,
grounded path finding around obstacles or gaps, persistent multi-tick
navigator/head/look state, and accepted wander/watch/tempt/follow-parent tasks
also remain open.

This does not promote the surrounding route-level passive spawn algorithm.
That path is still explicitly hash-based and approximate for candidate
position, type and pack ordering; only its color selection after choosing a
sheep uses the exact shared World cursor. Full `WorldEntitySpawner` parity,
including all earlier World.rand consumption, remains open.

Still open at this boundary: looting modifiers and other killer/Forge XP
branches, full natural passive pack ordering, broader mob loot
tables, hostile and child/silent status/sound paths, terminal and landing/break
particle/audio consumers, and exact mob/item/fire/fleece/yaw/hover persistence
in state capsules. Shears breaking at metadata 238 removes the tool, but the
client-side broken-item particles/sound and their additional RNG consumption
remain open. Offhand shearing is proven only when the main hand does not
successfully consume the use action; general two-hand item-use precedence is
broader work. Ordinary age progression and grass-eating AI are not implied by
the old cold child-age fixture; they are now represented for ordinary
AI-enabled passive ticks, while swim/tempt/follow task conflicts, broader
mate-navigation conflicts, navigation interruption beyond represented panic,
partial-tick client head pose, and real-Java grazing pixels remain open.
Non-sheep breeding, player breeding statistics and criteria, and breeding
state-capsule persistence also remain open. The native fixed item pool also has an explicit non-Java
capacity rule: if every required stack cannot fit, the target is rejected
atomically without health, RNG, ID, event, or drop mutation.

The earlier instant-only native aggregate passes in 5:08.99 at 370,040 KB peak RSS with zero
swap at `c/magma/trace/out/test_runtime_falling_instant.time`; the scalar guard
passes at 5,103 steps/s at
`c/magma/trace/out/perf_guard_falling_instant_cpu_1.json`. Ordinary false mode
has no added idle scan; synchronous work is callback-local.

Capsule restore admits supported canonical anvil callbacks and rejects a
falling callback because ordinary world saves do not contain the future
clock-seeded entity cursor. That positive/negative contract passes at
`c/magma/trace/out/state_capsule_anvil_1.log`. The earlier drop-only aggregate
passes in 6:07.55 at 370,148 KB peak RSS with zero swap at
`c/magma/trace/out/test_runtime_anvil_drop.time`; the scalar guard passes at
5,183 steps/s at
`c/magma/trace/out/perf_guard_falling_anvil_drop_cpu_1.json`. General impact
damage beyond the represented player and controlled sheep/pig/cow/chicken slices, including
other living types, enchanted/multi-piece armor, other effects, and general
multi-target ordering, remains open. World-event consumers, item-pool
pressure, area-not-loaded falling, moving pistons, and
fluid/collision generality also remain open. GPU 1 was not used.

The fire callback's outer `doFireTick` guard is now exact. The capsule carries
the captured global value into magma, retains a disabled pending block-51
entry, and drains it at the due boundary without changing the fire or adjacent
plank, consuming callback RNG, or scheduling a successor. The strict disabled
row and enabled scheduled control pass together at
`c/magma/trace/out/matrix_fire_rule_pair/summary.md`; the enabled direct
callback remains strict at
`c/magma/trace/out/matrix_fire_callback_control/summary.md`. Three disabled
repeats and the exact-source clean-build replay also pass at
`c/magma/trace/out/matrix_fire_tick_disabled_repeat_3/summary.md` and
`c/magma/trace/out/matrix_fire_tick_disabled_clean_final/summary.md`. The full
native aggregate passes in 7:10.18 with a 306,748 KB peak and zero swap at
`c/magma/trace/out/test_runtime_fire_disabled.log`. Its scalar recapture ran
under an unrelated 64-thread TAK workload and is deliberately not promoted;
the callback-local guard changes no `mc-sim` code and adds no idle scan or
allocation. Broader rain behavior, unsupported flammable
blocks, portals, and general loaded-world random-tick
selection remain open.
The isolated age-four burnout and every admitted dimension/source pair for the
represented infinite-source rules are
now exact in the bounded dry/normal proof. Ordinary stone fire consumes its
two age/reschedule draws, leaves the stale successor queued, and becomes air.
Age-15 fire on Overworld or Nether netherrack, or End bedrock, survives,
consumes the exact seven-draw direct callback, and retains one source schedule.
The Overworld netherrack
scheduled/direct repeat set passes 6/6 at
`c/magma/trace/out/matrix_fire_netherrack_source_repeat_3/summary.md`; three End
direct repeats pass over 22,869 cells at
`c/magma/trace/out/matrix_fire_end_bedrock_source_callback_repeat_3/summary.md`.
Three Nether direct repeats pass over 4,913 cells at
`c/magma/trace/out/matrix_fire_nether_netherrack_repeat_3/summary.md`. All 16
current fire/weather controls pass together at
`c/magma/trace/out/matrix_fire_weather_nether_family/summary.md`. Native delayed
dispatch for all three source configurations passes in the final aggregate at
`c/magma/trace/out/test_runtime_fire_nether_netherrack.log`, and the last clean scalar
guard passes at 4,282 steps/s at
`c/magma/trace/out/perf_guard_fire_eternal_sources_cpu_1.json`. Broader rain,
flammable materials, portals, and unconstrained loaded-world
callback interleaving remain open.
The adjacent TNT ignition branch is exact in the same bounded dry proof. Java
first replaces the TNT according to the ordinary fire roll, then invokes the
saved TNT state's EXPLODE destruction hook. With `Random(4)`, both engines age
the source from zero to one, replace only the east TNT with air, preserve the
source schedule, consume the exact nine World RNG and two Math RNG draws,
allocate one exact entity ID, and expose the same one-tick primed TNT at fuse
79 with matching motion. The pre-fix row already matched all 10,625 raw cells
but diverged only on entity and causal cursor state at
`c/magma/trace/out/matrix_fire_tnt_candidate_red_2/summary.md`. The fixed row
passes three independent repeats at
`c/magma/trace/out/matrix_fire_tnt_repeat_3/summary.md`; all 17 current
fire/weather controls pass together at
`c/magma/trace/out/matrix_fire_weather_tnt_family/summary.md`. The native
aggregate covers fixed-pool rejection before mutation and the successful
constructor/tick path, passing in 5:28.44 with a 315,368 KB peak and exit zero
at `c/magma/trace/out/test_runtime_fire_tnt.time`. The added branch is reached
only after a successful active fire-target roll and uses the existing fixed
TNT pool; it adds no allocation or idle scan. More flammable
materials, portals, and unconstrained loaded-world callback interleaving
remain open.

Source-column high humidity is now exact inside the bounded dry NORMAL fire
proof. The capsule transports Java's authoritative
`isBlockinHighHumidity(source)` result for one admitted callback rather than
deriving swamp humidity from C worldgen. Java reuses that one predicate for
both effects: subtracting 50 from all six direct-target denominators and
halving the volumetric spread threshold.
`Random(0)` is the strict direct discriminator: normal `nextInt(300)=229`
leaves the east TNT intact, while swamp `nextInt(250)=29` burns it and primes
the exact fuse-79 TNT. The pre-fix humid row differs in only the TNT cell,
entity state, and causal cursors at
`c/magma/trace/out/matrix_fire_humidity_probe_2/summary.md`; the corrected pair
passes at
`c/magma/trace/out/matrix_fire_humidity_candidate_fix_1/summary.md`.
`Random(776)` separately proves the volumetric rule: after both direct paths
miss, the first candidate roll is two, which normal threshold two admits as
age-one fire with a +39 child, while humid threshold one rejects it. Both
spread rows pass 3/3 at
`c/magma/trace/out/matrix_fire_humidity_spread_repeat_3/summary.md`, and all 21
affected fire/weather rows pass together at
`c/magma/trace/out/matrix_fire_weather_humidity_spread_family/summary.md`.
The full helper/runtime aggregate passes in 6:39.05 at 321,096 KB peak with
exit zero at `c/magma/trace/out/test_runtime_fire_humidity.time`; the runtime
rerun containing the final spread assertions passes in 5:53.34 at 253,264 KB
with zero major faults at
`c/magma/trace/out/test_runtime_fire_humidity_spread.time`. The new work is
bounded to an active fire callback, uses scalar context and fixed queues, and
adds no idle scan or allocation. Performance promotion remains deferred under
the unrelated 64-thread host workload, so the promoted total stays 676.
The first bounded rain branch is now exact. The oracle stages steady rain with
normal weather-cycle ticking, waits for effective server/client rain, and
captures both weather timers plus `isRainingAt` for the source, west, east,
north, and south probes used by `BlockFire.canDie`. The capsule admits only one
Overworld age-15 fire on stone with a non-humid NORMAL context and a complete
inert spread neighborhood; magma transports the Java exposure result instead
of inferring biome precipitation from C worldgen. `Random(1024)` produces
`nextFloat=0.6392364501953125`, removes the fire before rescheduling, and ends
at cursor `0xA3A500C65674`. The `Random(0)` negative produces
`0.7309677600860596`, consumes the following `nextInt(10)=8`, queues the exact
+38 stale successor, and then removes the isolated age-15 fire. Scheduled and
parked direct versions of both branches pass for a directly exposed source and
for a covered source whose four cardinal probes are exposed. The latter proves
Java's neighbor-only `canDie` disjunction without adding a new runtime path. A
callback-local BlockFire mixin now restores and records the armed World.rand
cursor immediately inside the real scheduled callback; this replaced a flaky
server-tick-start reset that allowed unrelated queued work to consume the seed.
All 18 independent branch/topology repeats pass at
`c/magma/trace/out/matrix_fire_rain_exact_hook_repeat_3/summary.md`, including
the exact scheduled cursor transitions. Effective thunder is also transported
and no longer rejected by the same bounded runtime proof; vanilla BlockFire
does not branch on thunder separately. Scheduled/direct thunder rows pass 6/6
at `c/magma/trace/out/matrix_fire_thunder_age15_repeat_3/summary.md`. Nether
netherrack is now exact in dimension -1 too. Adjacent fire-to-TNT ignition is
exact as well, and all 17
clear/gamerule/source/rain/thunder rows remain strict together at
`c/magma/trace/out/matrix_fire_weather_tnt_family/summary.md`. The final
native aggregate passes in 5:28.44 with a 315,368 KB peak and zero swap; the
last clean scalar guard
passes at 4,187 steps/s against the unchanged 3,858.9 floor at
`c/magma/trace/out/perf_guard_fire_rain_age15_final_cpu.json`; current recaptures
at 3,148 and 3,576 are explicitly contaminated by an unrelated 64-thread job
at host load 81-85 and are not promoted. At that checkpoint, general
rain/thunder strength transitions, additional mixed-cover exposure topologies,
rain-aware spread into flammables beyond the direct-target slice, lightning,
precipitation rendering, and weather audio remained open; see the current
simulation/replay residual list below for the promoted weather subset.

Rain-aware direct target burning is exact for one bounded netherrack-source,
east-tall-grass topology. Tall grass is deliberately precipitation-transparent,
so an exposed and roofed pair distinguishes the target's real
`isRainingAt` predicate without changing the successful chance/fate rolls.
With public `Random(5)`, both callbacks age source fire zero-to-one. The
exposed target becomes air, consumes nine World RNG draws through
`0x72D7583447FB`, and creates no child; the covered target becomes age-zero
fire, consumes its age/schedule draws through `0xB29D468F3AAD`, and queues the
exact +35 child. The valid pre-fix red at
`c/magma/trace/out/matrix_fire_rain_direct_target_red_3/summary.md` has exactly
one wrong target cell plus its child-queue/cursor consequences, while the
roofed negative already passes. Both corrected rows pass at
`c/magma/trace/out/matrix_fire_rain_direct_target_candidate_1/summary.md` and
3/3 at
`c/magma/trace/out/matrix_fire_rain_direct_target_repeat_3/summary.md`; all 23
affected fire/weather rows pass together at
`c/magma/trace/out/matrix_fire_weather_rain_target_family/summary.md`. Native
coverage locks the wet no-age-draw branch and dry-under-roof source/child
queue. The runtime aggregate passes in 5:32.01 at 253,108 KB peak with zero
major faults and exit zero at
`c/magma/trace/out/test_runtime_fire_rain_direct_target.time`. Context checks
and the extra boolean are active-callback-only with no allocation or idle
scan.

Rain-aware volumetric candidate suppression is exact for one bounded west-air
candidate beside an eternal netherrack source. The paired fixture uses a
precipitation-transparent carpet below the candidate; the covered control adds
only five stone roofs two cells above the candidate and its cardinal
neighbors. With public `Random(125)`, both callbacks reach the first volume
roll `0 <= 3`. The exposed candidate's true `canDie` predicate suppresses it
before age or schedule draws and ends at World cursor `0x06F23450DB83` with
zero mutations. The covered candidate becomes age-zero fire, queues the exact
+35 child, and ends at `0xE9AD9F0B0D75`. The valid pre-fix row differs at only
that candidate cell plus its queue/cursor consequences at
`c/magma/trace/out/matrix_fire_rain_volume_red_3/summary.md`; the covered
negative already passes. Corrected rows pass at
`c/magma/trace/out/matrix_fire_rain_volume_candidate_1/summary.md` and 3/3 at
`c/magma/trace/out/matrix_fire_rain_volume_repeat_3_final/summary.md`. All 25
affected fire/weather rows pass together at
`c/magma/trace/out/matrix_fire_weather_rain_volume_family_final/summary.md`.
The native aggregate locks both outcomes and passes in 5:40.24 at 252,144 KB peak with
zero major faults and exit zero at
`c/magma/trace/out/test_runtime_fire_rain_volume.time`. The new predicate is a
single transported callback-local boolean with no world scan, allocation, or
inactive-path work. Broader candidate positions and material/topology coverage
remain open.

The first non-plank fire material-table row is now admitted. White wool uses
Java's existing encouragement 30 and flammability 60 values rather than a
plank surrogate. With public `Random(36)`, its east direct roll is `11 < 60`,
the target becomes age-zero fire, both source and child queue at +35, and the
exact eleven-draw World cursor ends at `0x8EBD372F3662`. The previous runtime
rejected the capsule at its proof fence before C could execute the already
correct table row; that unavailable boundary is retained at
`c/magma/trace/out/matrix_fire_wool_red_1/summary.md`. The corrected row passes
at `c/magma/trace/out/matrix_fire_wool_candidate_1/summary.md`, 3/3 at
`c/magma/trace/out/matrix_fire_wool_repeat_3/summary.md`, and with all 26
affected fire/weather cases at
`c/magma/trace/out/matrix_fire_weather_wool_family/summary.md`. Native coverage
passes in 6:32.33 at 324,036 KB peak with zero major faults and exit zero at
`c/magma/trace/out/test_runtime_fire_wool.time`. This adds two active-proof ID
comparisons and no callback-kernel, allocation, or inactive-path work. Other
flammable material rows remain open.

The first low-flammability material row is now admitted too. Oak logs use
encouragement 5 and flammability 5. Public `Random(57)` leaves source age zero,
queues it at +31, burns only the east log to age-zero fire, queues that child
at +38, and finishes the exact eleven-draw World cursor at
`0x27DB2C1FBC09`; all other controlled cursors remain unchanged. The strict
candidate and 3/3 independent repeats pass at
`c/magma/trace/out/matrix_fire_log_candidate_1/summary.md` and
`c/magma/trace/out/matrix_fire_log_repeat_3/summary.md`. All 29 then-current
fire/weather controls pass together at
`c/magma/trace/out/matrix_fire_weather_log_family/summary.md`. This widens only
the capsule/runtime proof fence around the already-correct material table and
adds no callback-kernel, allocation, scan, or inactive-path work.

The dry tall-grass row is exact as well. Public `Random(4)` advances source
fire from age zero to one, burns east 31:1 to air, creates no child callback,
and finishes the exact nine-draw World cursor at `0x1411389CAF08`; all other
controlled cursors remain unchanged. The candidate and 3/3 repeats pass at
`c/magma/trace/out/matrix_fire_tallgrass_candidate_1/summary.md` and
`c/magma/trace/out/matrix_fire_tallgrass_repeat_3/summary.md`. All 30 affected
fire/weather rows pass at
`c/magma/trace/out/matrix_fire_weather_log_tallgrass_family/summary.md`.
Together with grass-path falling, the final native aggregate passes in
6:09.90 at 330,292 KB peak RSS, zero major faults, zero swap, and exit zero at
`c/magma/trace/out/test_runtime_tallgrass_grass_path.log`. The clean scalar
guard passes at 5,102 steps/s against the frozen 4,062 baseline and 3,858.9
floor at
`c/magma/trace/out/perf_guard_tallgrass_grass_path_cpu_1.json`; GPU 1 remains
shared and untouched.

Bookshelf now has its exact full fire-table row, not only direct burn
admission. Java specifies encouragement/flammability 30/20; both runtime and
shared CPU/CUDA tables incorrectly grouped id 47 with encouragement 5. A
direct `Random(36)` case already passed flammability 20, while an isolated
netherrack-source volume case made Java accept `Random(263)` roll 2 against
threshold 2 and old C reject it against threshold 1. That one-cell, one-child,
two-cursor causal red is retained at
`c/magma/trace/out/matrix_fire_bookshelf_table_red_1/summary.md`. Both corrected
rows pass at
`c/magma/trace/out/matrix_fire_bookshelf_table_candidate_1/summary.md`, 6/6 at
`c/magma/trace/out/matrix_fire_bookshelf_table_repeat_3/summary.md`, and with
all 32 fire/weather controls at
`c/magma/trace/out/matrix_fire_bookshelf_table_family/summary.md`. The shared
host/device table's CPU assertion passes 37 lines at
`c/magma/trace/out/test_world_tick_bookshelf_table_cpu.log`; CUDA execution is
deferred while GPU 1 is shared. Together with soul sand, the full native
aggregate passes in 6:38.12 at 335,628 KB peak, zero major faults, zero swap,
and exit zero at
`c/magma/trace/out/test_runtime_soul_sand_bookshelf.log`. The clean scalar
guard passes at 4,853 steps/s against the frozen 4,062 baseline and 3,858.9
floor at `c/magma/trace/out/perf_guard_soul_sand_bookshelf_cpu_1.json`.

Hay and carpet now have explicit coverage for their shared 60/20 fire-table
row. Direct `Random(36)` proves hay flammability 20; a netherrack-source
`Random(391)` volume case reaches roll 3, proving encouragement 60 rather than
bookshelf's 30. Both Java and magma create only the intended age-zero child,
preserve the due-time-sorted source/child queue, and end at cursor
`0xF572AB2A46D7`. The shared CPU table asserts ids 170 and 171 in 38 lines at
`c/magma/trace/out/test_world_tick_hay_carpet_table_cpu.log`; CUDA execution
remains deferred while GPU 1 is shared. Together with the enchanting-table
falling case, the native aggregate passes in 6:48.67 at 346,044 KB with zero
major faults or swap at
`c/magma/trace/out/test_runtime_enchanting_table_hay.log`, and the scalar guard
passes at 4,671 steps/s at
`c/magma/trace/out/perf_guard_enchanting_table_hay_cpu_1.json`. Other
flammable-material topologies and the separate hash-RNG fire subset in
`block_tickers.h` remain open.

Fire-driven Nether portal activation now follows `BlockFire.onBlockAdded` in
the Overworld and Nether. A controlled tick-zero fire placement inside the
smallest 2x3 X-axis obsidian frame makes six portal 90:1 cells atomically,
schedules no fire callback, and consumes no World/Math/Block/update-LCG/entity
cursor. The valid old-runtime row retains exact non-block state but differs at
all six interior cells at
`c/magma/trace/out/matrix_fire_portal_red_1/summary.md`. A paired frame missing
one top obsidian proves the negative: fire remains 51:0, public `Random(36)`
consumes only `nextInt(10)=9` through cursor `0xBA4D5B0FA320`, and queues the
exact +39 callback. Both corrected rows pass at
`c/magma/trace/out/matrix_fire_portal_pair_candidate_1/summary.md` and 3/3 at
`c/magma/trace/out/matrix_fire_portal_pair_repeat_3_final/summary.md`; all 28
affected fire/weather rows pass at
`c/magma/trace/out/matrix_fire_weather_portal_family_final/summary.md`.
The complementary Z-axis frame now produces six 90:2 cells with the same zero
queue/cursor contract. A corrected wide-Y capture also establishes the first
true staging boundary: the old fire-centered 32-cube omitted the top obsidian
of a legal 2x16 frame, leaving fire plus 31 air cells where Java made 32 portal
cells at
`c/magma/trace/out/matrix_fire_portal_height_red_2/summary.md`. The live
adapter now performs Java's bottom-row/floor-qualified edge scans and aligns
the existing 32-cube to any legal 2..21 by 3..21 candidate; it does not enlarge
the shared CPU/CUDA `NpWorld`. The corrected tall row passes at
`c/magma/trace/out/matrix_fire_portal_height_candidate_2/summary.md`; Z/tall
promotion repeats pass 6/6 at
`c/magma/trace/out/matrix_fire_portal_axis_height_repeat_3/summary.md`, and all
31 affected fire/weather rows pass at
`c/magma/trace/out/matrix_fire_weather_portal_axis_height_family/summary.md`.
Focused native coverage proves both 21x21 axes, a broken maximum frame, and the
runtime Z/tall paths at
`c/magma/trace/out/test_portal_live_axis_bounds.log`. The final full aggregate,
including log fire, passes in 6:17.39 at 331,732 KB peak with zero major faults
and exit zero at
`c/magma/trace/out/test_runtime_fire_log_portal_axis.log` with timing metadata
at `c/magma/trace/out/test_runtime_fire_log_portal_axis.time`. The clean scalar
guard passes at 4,392 steps/s against the frozen 4,062 baseline and 3,858.9
floor at
`c/magma/trace/out/perf_guard_fire_log_portal_axis_cpu_1.json`. A bounded
necessary obsidian precheck keeps this work off ordinary new-fire additions;
no portal work enters idle ticks or recurring source-fire updates. Activation
from additional fire creation topologies remains to be promoted separately.

Specialized collision-ray overrides, additional shaped occluders, armored
mobs, loot drops, occluded/multiple or tagged items, other projectiles,
interleaved mixed-type ordering, dragon-respawn crystal abort/reset, the full
dragon phase graph and its shared entity-RNG consumer interleaving, lethal
crystal-notification transition, beam-region texel-selection residual, broader
dragon rendering, broader fire, emitted sound, and particles remain open.

## Interactive C raster renderer

### First-person hand use poses

All three states have exact Java A/B captures but remain strict C residuals.
Ownership is the union of the Java and C subjects at threshold zero.

- Bow pull: `hard_px=30260`, `maxch=125`.
- Eat mid-use: `hard_px=101880`, `maxch=215`.
- Blocking shield: the old idle-tip golden was replaced by a genuine sticky
  `MAIN_HAND/BLOCK` capture; C still has `hard_px=28506`, `maxch=100`.

The former hand mean budget is gone. Synthetic exact controls pass, while
missing Java silhouette, C-extra, +1-channel, shift, and recolor mutations all
fail.

Likely work: finish ItemRenderer/model registration, edge shading, and the
remaining bow/eat sticky full-use capture provenance.

Repro:

```bash
bash verify/ui_hud/run_ui_hud_gates.sh
```

### Inventory player preview

GUI chrome is bit-exact, but the rendered player remains open at max channel 1:

- Pose 1: mean `0.011641`, `442` nonzero pixels.
- Pose 2: mean `0.009949`, `323` nonzero pixels.

The current path matches RenderHelper/Mesa unorm8 light-material packing for the
dominant face bins. Smaller fixed-function primary-light bins remain.

Repro:

```bash
bash verify/mc_capture/run_gui_verify.sh
```

### Portal and underwater

- Portal is `CAPTURE_BLOCKED`: the accepted pair has max-channel-1 A/B
  instability. Atomic same-client-turn `frame_pair`, sticky portal time, and
  pinned texture animation did not make a new pair exact, so no worse golden
  was promoted. C-vs-J composition also retains a large outdoor-underlay
  residual.
- Underwater is `CAPTURE_BLOCKED`: A/B reaches max channel 3 and C-vs-J remains
  about `4.97/ch`. Eye-at-surface fog is also visibly too weak.

Neither surface may pass until the Oracle pair is exact and the C full-frame
residual is zero.

### Entity and particle pixels

No strict entity family is pixel-perfect yet:

- Atomic same-client capture makes Java A/B exact for all 16 hard states:
  slime/magma sizes and squish, dig stone/grass, both fireballs, dragon death
  at ticks 50/100/190, and XP orb. Zero states remain `CAPTURE_BLOCKED`.
- All 16 stable states are honest `RESIDUAL`; the default hard gate reports
  zero passes, 16 residuals, zero blocked states, and zero harness failures.
- XP is a genuine visible, full-frame A/B-exact Oracle capture; C remains
  `hard_px=11963` with mean channel error `2.263`.
- Small fireball no longer draws on-fire layers unless `isBurning`, but its
  complete ROI remains open.
- Dragon death uses the correct 48-bit `java.util.Random`; remaining gaps are
  body pose/UV/dissolve, fine ray orientation, and surrounding scene pixels.
- The death burst (deathTicks 180-217) now reconstructs the full vanilla
  timeline - every one of a `ParticleExplosionHuge`'s 8 batches (not just the
  newest), and the ~17 ticks of cloud that outlive the entity - and the boss
  fog now ends at `processDragonDeath` as vanilla does, which is what makes
  the post-death cloud read white instead of fog grey. What stays open is
  placement: the offsets come from `EntityDragon.rand` / `Particle.rand`,
  neither recorded in a tape and both seeded from system time, so magma's
  cloud matches the oracle's extent, brightness, and decay but not puff for
  puff (~0.6 IoU on the bright mask). Exact match needs the recorder to log
  `spawnParticle` calls.
- Death dissolve is still per-box rather than vanilla per-texel.

Repro:

```bash
bash verify/ui_entities/run_gates.sh
bash verify/ui_entities/run_oracle_gate.sh
```

### Blaze attack state (live core fixed; trajectory/RNG remains)

Fixed for replay (2026-07-29, `wt/blazeglow`): the blaze now renders
full-bright and engulfed in fire whenever the recorded entity flags say
`isBurning()`. Vanilla mechanism, both in `java/oracle-src`:

- `EntityBlaze.getBrightnessForRender` (`EntityBlaze.java:99-102`) returns
  `15728880 = (240<<16)|240`, i.e. lightmap sky 15 / block 15 regardless of the
  world cell. `RenderBlaze` itself is a plain `RenderLiving` with no glow
  layer, so ALL of the oracle's "highlighted" look is this override.
- `EntityBlaze.isBurning()` (`:172-175`) is overridden to `isCharged()`
  (`:180-186`), the `ON_FIRE` datamanager byte bit 0 that
  `AIFireballAttack.updateTask` sets at `attackStep == 1` and clears at step 5
  / `resetTask` (`:246`, `:281-291`) - 78 ticks on, 100 off. That is what
  `Render.doRenderShadowAndFire` (`Render.java:344-348`) tests before drawing
  `renderEntityOnFire`'s layers, so an aggroed blaze burns and an idle one does
  not.

**Old tapes already carry this.** The recorder writes
`(isBurning?1:0)|(isSneaking?2)|(isInvisible?4)|(isChild?8)` per living entity
row (`Recorder.java` "flags bitfield"), `replay_tape.py` forwards it as
`ent_view.flags`, and `script.c` stores it in `GmEntityView.flags`. Measured on
the three 2026-07-22 blaze tapes: 388-603 burning blaze rows each, with exactly
the vanilla 78-on/100-off duty cycle (`blaze_melee` transitions t=18, 93, 191,
269, ...). No recorder change was needed and none was made. Nothing was
inferred from "the blaze looks aggroed" - the reverted `60f4076` failure mode.

The live core was ported on 2026-08-02. Each active blaze now keeps the task's
`attackStep`, `attackTime`, running, charged, and retained-shot state. It uses
the exact 60-tick windup, three shots six ticks apart, 100-tick rest, immediate
charge clear on task reset, and attack-step reset with a preserved timer on
reacquisition. Close range switches to the six-point melee attribute. Views
carry `isBurning` from charged blazes and ordinary live fire counters.

Small fireballs now spawn at blaze center plus 0.5 Y with a real entity ID,
start at zero motion, and use normalized 0.1 acceleration followed by the
0.95 motion factor. Accepted entity hits deal five and call `setFire(5)`;
block hits place fire in the crossed face cell and no longer substitute an
explosion. Impact selection now shortens the entity segment at the nearest
block hit, intersects Java's player AABB expanded by 0.30000001192092896, and
uses shaped block bounding boxes instead of 0.25-block point samples. The
focused pre-fix ray gate missed a valid upper-corner player hit and falsely
hit the empty upper half of a lower slab; both cases pass after the port. The
cadence gate produces 60/66/72, proves 78 charged ticks plus 100 clear ticks,
reset/reacquire behavior, the first two projectile speeds 0.095 and 0.18525,
melee and projectile damage, daylight-burning render flags, and
non-destructive block impact.

The next live slice retains the blaze shooter's entity ID and applies
`ProjectileHelper`'s entity rule to represented mobs and boats: exact scaled
AABBs expanded by 0.30000001192092896, nearest strict intercept, shooter
exclusion through `ticksInAir=24`, and inclusion from tick 25. Non-immune
living hits use five damage plus `setFire(5)` without shortening a longer fire
duration; blaze, ghast, magma cube, pigman, and wither skeleton immunity is
honored. `--mob-griefing on|off` now carries vanilla's default-on rule into
runtime state. A living-shooter block impact with the rule off consumes the
projectile without placing fire, while a shooterless fireball still ignites.
`BlockStairs.collisionRayTrace` now reuses the runtime's exact actual-state
slab/quarter/eighth boxes, including connected inner and outer shapes. The
only other 1.11.2 block override, `BlockPistonMoving`, is also exact: its ray
method always returns null even while physical collision boxes exist.

Blaze and fireball randomness now use separate fixed per-entity
`JavaGaussianRandom` state. The blaze attack task consumes two Gaussians at
the shot boundary after the nested Java-float distance square roots; the new
fireball consumes three more from its own post-UUID stream and normalizes
through `MathHelper.sqrt`'s float result. The cached second Gaussian and 48-bit
cursor are restorable without allocation. Java, CPU, and CUDA agree on all 17
raw-bit/state outputs for public seeds 12345, 0, and `Long.MIN_VALUE`.
Live blazes also apply the exact airborne 0.6 fall damping, 100-tick
height-offset redraw, 0.30000001192092896 vertical impulse, gravity, and drag
order. Native negative controls distinguish the old centerline shot and old
non-floating path.

The independent live trajectory gate is
`trace/run_small_fireball_trajectory_regression.sh`. It restores one valid
parked `EntitySmallFireball` state into the real Java server and magma, then
compares eight ordinary ticks. Entity set, position, motion, and acceleration
match on every tick; the shared 10,625-cell world slice is also exact. This
gate uses the existing single-client trace path and does not depend on the
oracle pool.

The final projectile candidate slice includes the represented dragon's eight
multipart boxes and all ten end crystals in the same nearest strict-intercept
selection as players and mobs. It uses the vanilla post-update part sizes and
0.30000001192092896 expansion. A part consumes a non-player small fireball
without reducing parent health; a crystal is destroyed at the chosen impact
and supplies its exact center to the existing strength-six explosion path.
Native positive, miss, nearest-part, crystal, live-consumption, and no-damage
controls pass. The scan is bounded to 18 candidates and runs only for an
active small fireball in a world with an initialized dragon.

`raster/verify/scenarios/blaze_attack_cycle.yaml` is the real-game anchor. On
`scenario_blaze_attack_cycle_20260802T010552Z`, the second uncontaminated Java
cycle charges at recorder tick 184, creates distinct small fireballs at
243/249/255, and clears at 261; those client observations are the 60/6/6/6
server schedule across the recorder boundary. Its inventory gate passes and
the structural pixel gate passes over 41 frames. The replay's sole physics
failure is the known one-tick recorder-start `on_ground` pulse with zero
position drift; one later world-hash sample also differs, so this tape is
evidence for entity timing/pixels, not a promoted complete replay.

B-03's scoped live blaze and small-fireball behaviors are closed. Exact full
dragon movement-history/part pose remains broader End/entity parity work and
is not implied by the projectile-candidate gate.

### Full-frame soft surfaces

These have useful capture-integrity checks but no pixel-perfect product claim:

- Death-screen world/tint composition outside the exact chrome and paired tint
  model remains about `33.17/ch`.
- Fire overlay full-frame composition remains open.
- Rain/overcast rendering is not modeled for the canonical rain window.
- High-altitude and long-distance haze are weaker than Java.

### Canonical tape residual unexplained clusters

The dominant early-tape failure (all CUTOUT geometry discarded: tallgrass,
cross plants, grass_side_overlay) was a misaligned positional `CrShadeCtx`
initializer and is **fixed** - see DEVLOG 2026-07-25. What remains on
`20260721T215812Z_fast_s0_survival_default_rd8_77b5b462`:

- Pixel gate still FAIL, but 7 failed frames (was 58), worst t=260 with
  7291 unexplained px (was t=80 / 74783). UNEXPLAINED total 122_581 px over
  63 frames (was 1_540_406 over 67).
- t=260 and t=460 hold the two big residual clusters (3380 px and 2989 px at
  t=260, on the near canopy and a distant tree). Re-measured 2026-07-25 with
  `pxdiff.py`: t=260 is 103 clusters, the canopy one at y[83,178] x[526,611]
  is 2989 px, `texel-selection`, exact-match 0.55 / tol4 0.83 at shift (1,-1).
  It is NOT a cutout-coverage bug, and the oracle's own capture settings say
  why it cannot be: the tape ran `fancyGraphics=false, mipmapLevels=0`, where
  vanilla `BlockLeaves.getBlockLayer` returns SOLID, not CUTOUT_MIPPED, and
  magma already meshes leaves as `CR_LAYER_SOLID` with alpha forced opaque.
  Direct count of the canopy bbox: 118 of 3991 differing pixels (3.0%) are
  true sky-holes, the other 96% are both-leaf texel flips. An earlier pxdiff
  build called this `cutout-sky+` from the mean-delta direction alone; the
  discriminator now requires a measured hole fraction, because on a minified
  canopy sigma is 50-70 and a mean of +4 along the sky axis gives an
  alignment of 0.997 at 3% coverage error.
  The leaf INTERIOR, separately, is nearest-neighbour texel selection on
  minified faces, not shading or geometry: the per-channel delta there is
  zero-mean with a large spread (near canopy mean +0.1/+0.2/+0.2, sigma 19/28/8; distant tree
  -0.9/-1.1/-0.4, sigma 32/46/14), i.e. individual texels flip between
  neighbouring values rather than the surface being uniformly off. Ruled out:
  a global sub-pixel camera offset (best whole-frame alignment is dx=dy=0,
  6.23 mean/ch, vs 7.45 at dx=-1) and the fog distance mode - the oracle's own
  GL query records `fog_distance_mode_nv = 34139` (GL_EYE_RADIAL_NV), which is
  what `CrFragment.eye_dist` already implements, and forcing planar |z| fog
  makes the tape worse (particles 181k -> 436k px, viewmodel 256k -> 411k,
  failed frames 7 -> 10).
- t=3180/3200/3220 clusters soak from `viewmodel` (hand/item residual), a
  separate open item under "First-person hand use poses".
- Residual whole-frame mean at t=80 is 3.76/ch, all outdoor terrain: grass
  tint / AO / luminance (the `known:4` class), not geometry.

Repro:

```bash
uv run --no-project --with numpy --with scipy --with pillow --with nbt \
  python verify/trace/replay_tape.py \
  verify/tapes/20260721T215812Z_fast_s0_survival_default_rd8_77b5b462.jsonl \
  --cpu --report
```

### Double-height plant model family (fixed 2026-08-02)

RESOLVED 2026-07-29. Two separate defects wore the same face. The model
mapping (upper-half meta quirk, per-type tint) was the first and is fixed
(90 model-oracle contracts). The residual "FLAT biome-tint rectangles"
was **not a renderer bug at all** - nothing in the plant texture path is
wrong:

- The mesh UVs are correct. Dumping every cross quad the scenic_walk
  replay emits gives tallgrass `uv=(0.191,0.500)..(0.247,0.562)`, i.e.
  the full 14x16-texel span of the sprite's atlas rect (48,128)-(64,144).
- The sampling is correct. Rendering the CUTOUT layer's interpolated UV
  showed a smooth gradient, and each rendered pixel equals
  `texel * tint * light` exactly (px(300,280): magma (70,109,52),
  sampled texel.r 149, tint (122,191,91) -> implied texel 146).
- The quads only LOOKED flat because they were point-blank: the biggest
  "solid" blob (11937 px, 142x118) spans **1.8 texels** of UV. It is one
  tallgrass card 0.81 blocks from the camera, magnified ~74 px/texel.

That card should not have existed. The recorded save has AIR at
(-171,70,247); the replay grew a plant there. `snapshot_patch.py` diffs
the save against `world_dump`'s generation and emits an event only where
they disagree - but the replay renders the GAME's generation, and the two
do not agree on decoration. magma's populate windows seed each other with
their neighbours' out-of-bounds spill (`world/populate_mc.c`
build_window donor seeding), so a window's cell list depends on which
windows were resident when it was built: `world_dump` builds them in one
fixed sweep, the game builds them around a walking player. Measured on
chunk (-11,15), seed 3: `world_dump` 24 tallgrass cells, the game 42,
agreeing on only 12. Every cell where the save and `world_dump` happened
to agree emitted no event and kept whatever the game grew there.

Fix: `snapshot_patch.py` now also states the save's value for the whole
vegetation band (4 blocks above each column's ground top), making the
patch authoritative exactly where generation drifts instead of trusting
`world_dump` to match. scenic_walk t=80 whole-frame 10.33 -> 3.39 /ch
(terrain 12.55 -> 3.13), tape mean 4.86 -> 3.34, unexplained gate pixels
4.03M -> 2.38M, failing frames 92 -> 39. Triptych:
~/dev/nw/.tmp/dp/scenic_t80_golden_before_after.png.

**Closed 2026-07-29.** The band was a workaround for diffing against the
wrong world; the patch now diffs against the right one. Census of the 169
chunks around the scenic_walk start (save vs `world_dump` vs the game's own
generation, `MAGMA_WORLDDUMP` in `game/script.c`): 4664 cells where the game
disagrees with the save while `world_dump` agrees with it, so no event was
emitted - and 4488 of them (96%) are logs and leaves, up to 30 blocks above
the ground the band is anchored to. A band cannot reach those. Ores are 1
cell and lakes 2: `WorldGenMinable` only reads base terrain, so ore placement
is effectively order-independent and never needed covering.

Fix: `snapshot_patch.py` runs a PROBE pass before it diffs - the tape's own
replay script with the patch reduced to its `snapshot_region` ensures and no
`snapshot_block` - and reads the world back out of the running game. That
probe builds populate windows in the real replay's order, because the order
is fixed by the ensure sequence and the simulated walk and neither depends on
the patch's block contents (`snapshot_region` is a plain `gm_world_ensure`,
and its tile list is derived from the tape). Verified: the per-tick player
chunk is identical across all 274 scenic_walk ticks with and without the
block events applied.

The patch is now exact rather than approximate, and much smaller for it.
Diffing the PATCHED replay's live world against the save, over the 289
chunks visible from the tape:

| tape | patch cells before | after | world cells still wrong |
|---|---|---|---|
| scenic_walk | 350646 | 66051 | 7046 -> 0 |
| slime_bounce | 302080 | 1465 | 0 -> 0 |
| hold_dig_dense | 295936 | 3 | 0 -> 0 |

Only scenic_walk was actually WRONG before; the other two were merely paying
~300k events to restate cells `world_dump` had guessed wrong but that the
game would have generated correctly anyway. scenic_walk's 7046 were 4517 tree
blocks, 2477 air where the save has a tree, 48 terrain, 1 ore, 1 plant. It is
now bit-identical to the save at
t=60/120/200/239/244/260/273, and the canonical 215812Z tape ends 3616 ticks
in with 5 differing cells, all of them blocks the session itself placed or
broke. Chunks the probe cannot observe (outside the resident 19x19 pool at
every dump tick) still fall back to `world_dump` plus the vegetation band;
that is 68 of 357 chunks on the canonical tape, 0 on the other three.

Consequence for the pixel gate: scenic_walk's remaining 39 failing frames
are NOT decoration. The world behind them is provably the save's, so the
residual is renderer-side (the failing frames' clusters are 40% particle
soak, the rest viewmodel and thin-line).

The earlier model-coverage work that preceded that world-state diagnosis:

The original 2026-07-29 scenic-walk capture showed id 175 as opaque green
geometry. The later canonical-state/model-key bridge had already removed that
stale slab path, but the surviving model table was still materially incomplete:
sunflower, syringa, rose, and paeonia lower halves collapsed to the grass
sprite, every upper half collapsed to grass, and the sunflower's tilted flower
head did not exist. Legacy upper metadata cannot select a species; vanilla
`BlockDoublePlant.getActualState` reads `VARIANT` from the lower half.

Fixed by preserving all six lower and upper jar texture pairs, applying grass
tint only to double grass and fern, selecting upper models contextually from
the lower canonical state, and porting `double_sunflower_top.json`'s two short
stem planes plus distinct tilted front/back head. The atlas generator appends
the ten previously missing textures so established sprite indices remain
stable.

Gates:

- `make verify-harsh` passes the jar-model, model-table, and emitted-mesh
  contracts, including all six contextual upper variants and exact sunflower
  vertex/sprite counts.
- `raster/verify/scenarios/double_plant_gallery.yaml` builds all six real Java
  pairs in one server tick. Tape
  `20260802T004204Z_fast_s0_creative_flat_rd8_0c4e00d6` replays with exact
  physics for 1,188 ticks, zero world-hash deltas, and a passing structural
  pixel gate over 119 frames. Evidence is at
  `trace/out/double_plant_gallery_valid_fix_1/` and
  `raster/verify/trace/report/tape_20260802T004204Z_fast_s0_creative_flat_rd8_0c4e00d6.md`.
- The full native runtime suite passes. GPU 1 performance passes at 4,780
  scalar steps/s, 2.93M Blaze env-ticks/s, and 31.37 1080p CUDA fps in
  `trace/out/perf_guard_double_plant_models_1.json`.

### Nether arrival: fire/lava content missing from the replayed world

Found 2026-07-29 on the first dense portal tape
(`scenario_portal_roundtrip_20260729T075228Z`); **root-caused and fixed the
same day**. Neither suspect in the original note was right: no filter dropped
fire (51) or lava, and the patch never covered DIM-1 because **there was no
DIM-1 to cover**.

- The recorder snapshots the save at `recstart` (`Recorder.java`, recstart
  handler). A dimension the player first enters DURING the recording has no
  region files on disk at that moment, so it can never be in that copy:
  `075228Z_world/DIM-1/` held only `data/` and `forcedchunks.dat`, and
  `snapshot_patch.py` emitted 0 dim -1 events (its cache is 29 events, all
  dim 0). The replayed Nether was therefore 100% magma's own generation.
- magma generates Nether TERRAIN (`nether_full.h` `nf_run` =
  ChunkProviderHell prepareHeights/buildSurfaces/MapGenCavesHell + fortress),
  which is why the cave geometry matched. It does not run
  `ChunkProviderHell.populate` - fire, lava springs, glowstone, quartz, magma
  blocks, mushrooms - and it cannot: that method's `Random` is reseeded only
  in `provideChunk` (`ChunkProviderHell.java:267`), so `populate` consumes
  whatever RNG state the previously generated chunk left behind. Nether
  decoration is chunk-load-order dependent, not seed-derivable. The saved
  world snapshot is the only sound mechanism for it.
- Second, independent defect on the replay side: `snapshot_arrival_events`
  detected arrivals only from position packets, and a portal transit has none
  (the server moves the player inside `changeDimension`). On this tape the
  row `dim` flips at t=133 and the first `ppos` is t=168, so even with DIM-1
  data the patch would only have been applied at tick 0, to a world the
  player was not in yet.

Fixes: `snapshotSaveDir(mc, snapRoot, addOnly)` in `Recorder.java` - the
recstart pass is unchanged, and `recstop` runs a second ADD-ONLY pass that
copies only paths the snapshot does not already have, so dimensions born
during the recording are added while recstart truth for the start dimension,
`level.dat` and `playerdata` is never overwritten with end-of-session state.
Plus the dim-flip arrival in `replay_tape.py::snapshot_arrival_events`.

The 075228Z tape is NOT repairable - its Nether was never written to any
disk that still exists - so the scenario was re-recorded with the fixed
recorder as `scenario_portal_roundtrip_20260729T083543Z` (dims {0:134,
-1:352}, `recstop` reported `snapshot_added: 4`, one per Nether region file).
Same-tape A/B, CPU replay (DIM-1 region hidden vs present, patch cache
dropped both times): 387 failed frames / 75.1M UNEXPLAINED px over 368
frames, worst t=292 at 266k -> 170 failed frames / 11.2M px over 143 frames,
worst t=281 at 175k. Fire and the arrival lava pool are present and the
chamber is lit in both panes (t=216, t=280 SBS).

Still open on that tape (170 frames): fire/lava ANIMATION phase and the
lightmap around them, plus the pre-existing viewmodel/HUD classes.

### Magma's generated nether lava sea is FLOWING lava: FIXED (2026-07-29)

Moved to CLOSED_DIVERGENCES.md.

### Eye-in-fluid overlay timing: CLOSED (root-caused 2026-07-29)

Moved to CLOSED_DIVERGENCES.md.

### Waterfall ENTRY window on the dense elytra tape (t=58..65) - CLOSED

Moved to CLOSED_DIVERGENCES.md.

### Fortress-hunt tape finds (2026-07-29, scenario_portal_fortress_blaze)

Three divergences from the staged fortress-melee recording
(`tapes/retired/scenario_portal_fortress_blaze_20260729T090129Z`):
- **Fortress placement**: `gm_fortress_locate`/`gm_fortress_spawner_room`
  and blaze `nether_full` both put seed-0's spawner room at
  (-325, 72, -151); the oracle's own DIM-1 region files have rooms at
  (-325, 56, -215) and (-325, 56, -102). x matches, y/z do not - the
  structure-gen port diverges beyond terrain.
- **Blaze death animation**: FIXED 2026-07-29. `gm_entities_emit`
  computed the `RenderLivingBase.applyRotations` keel and then threw it
  away (`(void)death_roll;  /* z-roll needs entity-level aff */`), and it
  tinted only on `hurtTime`, not `hurtTime || deathTime`. The tape has
  always carried both: entity row fields 10/11 are `hurtTime`/`deathTime`
  (t=274 is the first row with hp 0, hurtTime 9, deathTime 1; deathTime
  runs to 19 at t=292, then the entity despawns), `tape_to_script` writes
  them as ent_view `hurt`/`death` and `script.c` loads them into
  `GmEntityView`. Nothing needed inventing. `emit_box` now takes the keel
  cos/sin and applies it between the flipped model vector and the body
  yaw, matching the vanilla stack
  `translate(pos) . rotateY(180-yaw) . rotateZ(keel) . prepareScale`.
  Two conventions were settled by measurement, not assumption:
  partialTicks is 1.0 (partial=0 costs 155k unexplained px), and the red
  tint persists for the whole death (dropping it costs 44.7k px). Result
  on the repro tape: mean abs error over the death body falls at every
  death tick (t=281 42.6 -> 22.2/ch, t=283 27.3 -> 8.5/ch), tape
  UNEXPLAINED 5238675 -> 5124969 px. Note the keel saturates at 90 deg
  from deathTime 13, not 20 - `sqrt(deathTime*0.08)` hits 1 at 12.5.
- **Spawner cage miniature**: STILL OPEN, and it is a data gap, not a
  renderer gap. The renderer now exists and is verified
  (`gm_spawner_miniatures_emit`, `game/entity_render.c`; the exact
  `TileEntityMobSpawnerRenderer` stack - translate(x+0.5, y, z+0.5),
  translate(0,0.4,0), rotate(mobRotation*10) about Y, translate(0,-0.2,0),
  rotate(-30) about X, scale 0.53125/max(width,height), then the entity's
  own applyRotations/prepareScale - with unit coverage in
  `game/test_entity_render.c`). NOTHING CALLS IT, because no spawner's
  entity type reaches the renderer. Precisely what is missing, in order:
  1. `verify/trace/snapshot_patch.py` `_read_mca_states` reads only
     `Sections[].Blocks/Add/Data`; it never touches
     `chunk["Level"]["TileEntities"]`. The data IS in the region files -
     both fortress spawners in
     `..._world/DIM-1/region/r.-1.-1.mca` carry
     `SpawnData.id = "minecraft:blaze"` (1.11.2 string form, not the
     pre-1.9 `EntityId`) at (-325,56,-215) and (-325,56,-102).
  2. There is no script event that carries tile-entity payload. The
     snapshot path emits only `snapshot_region` and `snapshot_block`
     (id/meta), and `script.c` has no `set_tile_entity` handler.
  3. Magma's world store is block id + metadata only; there is no
     position-keyed tile-entity store fed from world data. The chest and
     furnace tables in `GmRuntime` are created lazily by player
     interaction, and `mob_live.c discover_spawners` GUESSES a spawner's
     mob by sniffing nearby blocks (nether brick within +/-4 -> blaze).
     That heuristic must NOT be used to drive the renderer - it would
     paint a blaze into every nether spawner by construction rather than
     by data, which is exactly the bug the pixel gate is supposed to
     catch.
  4. `frame_capture.c` has no tile-entity render pass; the miniature emit
     has to be drawn after the world layers with the mob atlas bound.
  The spawner visible in the approach frames (t~150-230) is the
  (-325,56,-102) one, whose NBT `RequiredPlayerRange` is 0. Vanilla's
  `MobSpawnerBaseLogic.updateSpawner` only advances `mobRotation` when a
  player is inside that range, so this miniature is FROZEN at rotation 0,
  not spinning - once the type flows, no rotation simulation is needed
  for this tape.
Harness notes that cost takes (now in the yaml): `structures: false`
disables fortresses entirely; melee attacks fire on the mouse-DOWN edge
so held button-1 lands exactly one swing (this is why the older
blaze_melee/blaze_bow tapes never damage their blaze); the target is
NoAI-pinned because a live blaze kites to fireball range.

### Dragon-kill tape finds (2026-07-29, scenario_dragon_kill)

From `tapes/retired/scenario_dragon_kill_20260729T094414Z` (pitch-armed
command-block kill so the real onDeathUpdate plays; see the yaml):
- **Death-ray intensity curve**: **root-caused and FIXED** (2026-07-29,
  `wt/dragonfx`). Three independent bugs in one symptom, all in the
  `LayerEnderDragonDeath` port:
  1. *Late onset*: `gm_dragon_death_rays_emit` skipped the entity while
     `(f+f*f)/2*60 < 1`. Vanilla's loop is `for (i = 0; (float)i < bound;
     ++i)`, so ANY bound > 0 draws one ray - deathTicks 1 already has a
     beam (`LayerEnderDragonDeath.doRenderLayer`). Cost: ~5 death ticks.
  2. *Too bright mid-death*: the ray pass ran with no fog and no
     lightmap. `EntityRenderer.setupFog` is scene state (entities are
     drawn under it), and the End dragon fight's `BossInfo.createFog`
     (`DragonFightManager.bossInfo` ... `setCreateFog(true)`,
     `GuiBossOverlay.shouldCreateFog`) pulls the linear ramp to
     `[far*0.05, min(far,192)*0.5]` = [6.4, 64] - the dragon sits 39-54
     blocks out, i.e. 57-83% fogged. Separately, the layer's
     `disableTexture2D()` only disables the ACTIVE unit, so the lightmap
     on `OpenGlHelper.lightmapTexUnit` keeps MODULATing the fans with the
     dragon's brightness (0.24 in the End, not 1.0). MixinStripBossBar
     hides only the HUD bar, never the BossInfo, so the fog is live for
     the whole animation even though no bar is in frame.
  3. *No final starburst*: `(int)(255.0F * (1.0F - f1))` goes NEGATIVE
     once deathTicks+partial > 200, and `VertexBuffer.color(int,int,int,
     int)` stores it through a Java `(byte)` narrowing cast (UBYTE
     branch) - it WRAPS to ~250. That wrap IS vanilla's t=458 starburst;
     magma clamped to 0 and the rays vanished at the oracle's peak. The
     `bound > 60` clamp went too: the CLIENT keeps ticking deathTicks
     past 200 (`setDead` is inside `!world.isRemote`).
  Measured on the tape, mean-abs/ch vs golden: t=260 0.683 -> 0.572,
  t=340 3.354 -> 0.932, t=458 8.484 -> 7.355; whole tape 3.307 -> 2.235,
  death window (t=258..470) 3.917 -> 1.066. Ray energy now tracks the
  oracle within 1-9% for every frame from deathTicks 100 to 198.
  Residual at t=458 only (oracle ~2x brighter in the far field): the
  EXPLOSION_HUGE cloud `onDeathUpdate` spawns at deathTicks 180-200, and
  magma stops drawing the dragon entirely after t=458 because the tape's
  `ents` rows stop while the oracle client renders it to deathTicks ~204.
- **Dragon boss bar**: **not a boss bar** (2026-07-29). Neither side
  draws one on this tape - the recorder's `strip.overlays` cancels
  `GuiBossOverlay.renderBossHealth` (MixinStripBossBar) and magma gates
  its own top-center bar on `MAGMA_STRIP_OVERLAYS`. The "small strip
  above the hotbar" is magma's ARMOR row: the scenario equips a leather
  chestplate whose only job is knockback resistance, and an ItemStack
  with an `AttributeModifiers` tag REPLACES the item's default modifiers
  (`ItemStack.getAttributeModifiers`), so vanilla's generic.armor total
  is 0 and no icons are drawn. magma derived 3 from the item id, which
  is all the tape carries. **Fixed in code, needs a re-record**: the
  recorder now writes the real total (`Recorder.recordTick`,
  `"armor":p.getTotalArmorValue()`), the replay emits `armor_view`, and
  `gm_runtime_tape_armor` overrides the item-derived guess.
- **Phantom HUD icon**: **root-caused, fixed in code, needs a re-record**
  (2026-07-29). It is the Resistance status icon (a shield in
  inventory.png), drawn because the scenario runs `effect @p resistance
  1000000 4 true` - the trailing `true` is hideParticles, and
  `GuiIngame.renderPotionEffects` wraps the whole icon blit in
  `if (potioneffect.doesShowParticles())`. The recorder's `pots` triples
  never carried that bit, so magma could only assume "visible". `pots`
  entries now carry `doesShowParticles` as a 4th field, `potion_view`
  takes `show_particles`, and `GmPotionEffectView.hide_particles` gates
  the blit (inverted so legacy rows keep vanilla's shown default - the
  wither_skeleton tape's icon must not disappear).
  Verified by replaying this tape's generated script with both fields
  injected (what a re-record would produce): armor row and icon go to 0
  px, matching the golden, and mean-abs drops further to t=260 0.121,
  t=340 0.482, t=458 6.901, whole tape 1.817. THIS RETIRED TAPE STILL
  SHOWS BOTH until it is re-recorded with the new recorder.
- **Entity interpolation / mirrored death pose**: **root-caused and
  FIXED** (2026-07-29, `wt/dragonfx`). Not view lag and not
  interpolation - two bugs in the `getMovementOffsets` trail ring, both
  in `gm_dragon_pose_tick` (`game/entity_render.c`). Nothing reads the
  entity's `rotationYaw` directly: `RenderDragon.applyRotations` and the
  whole `ModelDragon` neck/head/tail chain are driven ONLY by the ring,
  so a ring-phase error rotates and re-poses the entire dragon while
  leaving its translation correct - which is why the symptom read as a
  positional offset with no fixable `(dx,dy)`.
  1. *Ring phase one tick early*: vanilla's push
     (`EntityDragon.onLivingUpdate:239-240`) runs BEFORE the tick's own
     motion - the client interpolation block is at `:242-255` and the
     phase movement below it - so `ringBuffer[idx]` is the pose at the
     END of tick T-1 while the render at `partialTicks=1.0` draws the
     body at the end of tick T. The tape's `ents` row is post-tick
     state, so magma was pushing tick T. Fix: hold the current row in
     `pend_*` and push the PREVIOUS one, which makes `ring[]` a literal
     `ringBuffer[]` and keeps `er_dragon_mo` a literal
     `getMovementOffsets`.
  2. *Ring kept advancing after death*: `health <= 0` takes
     `onLivingUpdate`'s `:191-197` branch (explosion particles) and
     never reaches the push, so vanilla's ring - and with it the ENTIRE
     model pose, `animTime` included - freezes at death. Meanwhile
     `onDeathUpdate` spins `rotationYaw += 20` per tick
     (`EntityDragon.java:701`) and the recorder faithfully writes that
     spin into the tape (`yaw` runs 157.5 -> 797.5 over t=258..290,
     unwrapped, because the `wrapDegrees` at `:217` is in the alive
     branch). magma fed the spin to the ring, so its dying dragon
     rotated ~20 deg/tick and read as MIRRORED within ~9 death ticks.
  Measured with the geometry oracle (`<tape>.geom.jsonl` vs
  `MAGMA_GEOM_DUMP`, `geom_diff.py --offset 0`, ticks 180-458 - below
  180 the recorder's golden re-render is still stale, `jaw` is pinned at
  its t=0 value there). 1668 part comparisons: 255 mismatches, worst
  164.6 texels / 3.109 rad -> **0 mismatches, max 0.000 texel /
  0.0000 rad**. Dead ticks alone went 198/1188 bad -> 0. Pixel residual
  in the dragon window (x[300,620] y[100,370], per-pixel max-channel
  delta > 16, best-shift scan +-16 px): t=230 542 -> 183, t=244
  482 -> 195, t=260 617 -> 436, t=270 4957 -> 1954, t=280 5284 -> 2277.
  Best shift is (0,0) before AND after - there was never a translation
  to recover, confirming the "~16px right" reading was pose error.
  Residual after t=270 is the death-ray/dissolve pass, tracked above.

### Geared dragon-kill tape: the death clock desyncs (2026-07-29)

`scenario_dragon_kill_geared_20260730T025316Z` is the same bow kill with a
geared player, and after the burst rebuild above it failed the gate on two
frames (t=454 4855 px, t=456 4258 px, both magma-brighter UNEXPLAINED). It is
not a magma timing bug: **that recording's client and server death clocks are
6 ticks apart, and the tape records only the client's.**

Evidence, all from the tape's own frames:

- The death rays agree exactly. `LayerEnderDragonDeath` is driven by
  `deathTicks` through a fixed `Random(432L)`, so the spoke pattern is a
  fingerprint of the render clock. Magma's t=454 rays vs the oracle's:
  **IoU 0.900 at t=454 and 0.000 at 448, 450, 452, 456, 458, 460**. The
  recorded `deathTicks` IS the client's render clock, including the
  `(byte)(255*(1-f1))` alpha wrap that makes the dt-201 starburst.
- The cloud does not. The oracle's explosion cloud drops the dense
  `BossInfo` fog ramp (p90 33.7 grey -> 180.0 white, px>150 68 -> 3692)
  between t=446 and t=448, i.e. at recorded `deathTicks` 195 - six ticks
  before that same clock reaches 200. Particle brightness has no other
  input: `ParticleExplosionLarge` hardcodes `lightmap(0, 240)` and
  `explosion.png` is pure white with binary alpha, so only fog can move it.
- Fog is server state (`DragonFightManager.processDragonDeath` ->
  `setVisible(false)` at server `deathTicks` 200), so the server led the
  client by 6 ticks here. On the synced recording
  (`scenario_dragon_kill_20260729T110941Z`) both clocks coincide: white
  cloud at t=462, recorded dt 201, one tick after the entity leaves the
  tape.
- Ruled out with measurements: `VALID_PLAYER` scoping (player stationary at
  67.2 blocks, hp constant, never leaves the 192 sphere), launch profile
  (both `fast`, `strip.overlays`), `MixinStripBossBar` (cancels
  `renderBossHealth` only, BossInfo and its fog stay live), a row/frame skew
  (geared t=448-452 match no original frame at any dt, MAE 16-19 vs ~2), and
  a latch tick offset (magma's population crosses the oracle's between t=456
  and t=460 rather than sitting at a constant sign).

Nothing in the tape exposes the server clock: `processDragonDeath`'s only
other observables are XP orbs and the gateway, and the recorder's entity
whitelist logs neither. Magma therefore keeps snapping at the one server
event the tape does expose (`deathTicks` 200 / entity removal), which is
correct wherever the recording is synced. On this tape that puts magma's
white-cloud window 6 ticks late, which flips the residual bright-puff
placement mismatch - the unrecoverable `EntityDragon.rand` / `Particle.rand`
offsets of divergence 40 - onto the magma-brighter side at t=454-458. Extent
and decay still track (bright px 10571 vs 10535 at t=454, 4531 vs 4233 at
456, 3804 vs 3876 at 460), so those three frames are filed in the tape's
`known_divergences.json` sidecar as divergence 40, scoped to
`[170, 280, 300, 460]` and ticks 454-458.

### Scenario tape pixel-gate failures (triaged, unfixed)

Diagnosis only, from a delegated triage pass; the code claims below were
spot-checked but the pixel measurements are the triage agent's, not
independently re-measured here.

- `scenario_soulsand_ice_20260723T001810Z`: **closed** (2026-07-25). The 44
  mild-shift failures were the sky-plane fog: `orientCamera` puts the 64-tile
  sky plane at `16 - eyeHeight` and vanilla's fixed-function fog on it is
  per-vertex Gouraud, not per-pixel (`ac47c2b`). That left one frame, t=60, a
  77%-of-frame 5.37 shift on the step down onto soul sand: the `fogColor1`
  light smoother was sampling the post-tick feet, one tick ahead of vanilla's
  pre-movement `updateRenderer` (`5c4cf6e`). The tape is rc=0.
  Still open on this tape: ~1226 px of UNEXPLAINED at t=50, in 7 clusters of
  50-370 px (plus viewmodel-masked siblings), all mid-frame on the receding
  soul sand path with the grass either side clean. `pxdiff.py` on the
  texel-selection bands (e.g. y[320,331] x[407,444]): zero_shift 26.26/ch,
  best_shift dy=-1 at 1.52/ch; remeasured as C[y]≈G[y+1] at 0.4-1.5/ch
  across those rows. At yaw=-90 pitch=0 the screen-Y axis maps to texture U
  (world X / path depth), not V.
  **Sampling-rule dead end (2026-07-25, wt/texel):** magma already uses
  GL_NEAREST `floor(u*w)` (default is `floor(u*w - 1e-4)`; pure floor via
  MAGMA_SAMPLE_MODE=1 is bit-identical on this residual). Vanilla 1.11.2 with
  `mipmapLevels:0` binds the terrain atlas as plain GL_NEAREST (EntityRenderer
  only forces non-mip for CUTOUT; SOLID uses the upload filter, still nearest
  with no mips). Sweeps: round / UV+0.5 drop UNEXPLAINED to 165 but raise
  whole-frame mean 3.51→5.47 and shred HUD/viewmodel; U+0.2 yields unex 864
  mean 3.43 (partial, no principle); every V bias worsens both metrics. No
  sampling-operator change gets unex→0 without a nightly regression - do not
  fudge MAGMA_TEXEL_BIAS. Residual is UV phase at pixel centres (wrong side of
  texel boundaries on the oblique top faces), not floor-vs-round. Checked
  separately: `raster_cpu.c` already samples at `(px+0.5f, py+0.5f)`, so the
  other cheap phase suspect - rasterising at the pixel corner - is ruled out
  too. What is left to test is the quad's world position / perspective-correct
  UV precision on grazing top faces, which is a different investigation.
- `20260712T055346Z_fast_s0_survival_default_rd8_77b5b462`: the goldens have
  no HUD, and magma used to draw one. **`capture.hide_gui` is a misnomer** and
  the original diagnosis in this entry was wrong about the mechanism: the tape's
  own `qrl_launch` records `hide_gui: false` with `strip.overlays: true`, so the
  recorder skipped the overlay pass rather than Malmo forcing
  `gameSettings.hideGUI`. The goldens draw a first-person viewmodel on nearly
  every frame - a bare arm on most of them, a held item on a few - which
  settles it. That matters because vanilla's real `hideGUI` would take
  the hand and the portal wash with it; "overlays stripped" takes only the
  overlay, so the portal wash stays tied to this flag (it is drawn inside
  `renderGameOverlay`) while suppressing magma's HAND is a workaround for the
  arm's over-bright shading, not fidelity. `options.bobView` is also false in
  this recording, so the oracle's viewmodel never bobs - it sits on identical
  screen pixels frame to frame, which is what made the yaw-sweep test decisive.
  The original (wrong) framing follows, kept because the pixel numbers in it
  are real: the goldens have no HUD and no first-person hand (Malmo forces
  `hideGUI` for the mission),
  magma used to draw both, and the gate's positional `hud` and `viewmodel`
  accepts swallowed the whole mismatch - the bottom 96 rows plus the lower
  right quadrant had no pixel verification at all on this tape.
  `capture.hide_gui` + `MAGMA_HIDE_GUI` stops magma drawing them (`3dc2d19`,
  `81ea54e`), and the gate stops accepting those regions positionally when the
  tape sets that flag: `_positional_accept_masks(hide_gui=True)` returns empty
  HUD and viewmodel barriers. The legacy sidecar regions, all recorder gaps and
  all scene-global, were widened from y1=383 to y1=479 for the same reason:
  their old limit was an artifact of the mask, not of the gap.
  `hideGUI` covers more than the overlay. `EntityRenderer.renderHand` gates
  `renderItemInFirstPerson` on `(thirdPersonView == 0 && !sleeping &&
  !hideGUI && !spectator)` (oracle-src `EntityRenderer.java:824`), and
  `GuiIngame.renderPortal` is called from inside `renderGameOverlay`
  (`GuiIngame.java:156`). `itemRenderer.renderOverlays` (block-in-hand, water,
  fire) and `hurtCameraEffect` sit outside that branch at line 833 and stay on.
  Failed frames go 14 -> 25 -> 19. That is the price of measuring a quarter of
  the frame that was never measured before, on all 157 frames; the tape was
  already failing. The rain window t=1800..2100 now resolves cleanly into
  `known:12` where before it leaked, the four pure-arm frames
  (t=360/440/480/500) are gone, and what is left in the newly measured region
  is t=520 (near-field pit floor, same dirt palette in a different projection -
  the golden's floor texels streak, magma's stay square, everything outside the
  pit matches to 0.1 percent) and t=2440 / t=2860.
  t=2440 IS the hand, and an earlier revision of this entry said it was not.
  The transient brown object in the bottom-right corner is the oracle's held
  wooden shovel. `capture.hand_from_tick` (2440 here, carried to
  `frame_capture.c` as `MAGMA_HAND_FROM_TICK`) turns magma's hand back on from
  that tick: t=2440 goes 3.167 -> 0.969 per channel whole-frame and stops
  failing (`c4b7f00`), and at t=2860 both sides now draw the same shovel.
  **What that tick actually means** is not what `c4b7f00` claimed. It is not
  the tick where the oracle's viewmodel returns - the oracle draws a viewmodel
  through the whole tape. It is the first golden at which MAGMA's held item is
  finally known to be right. The tape rows carry no `inv` field at all; the
  only inventory magma ever receives is two `set_inventory` rows in
  `<tape>.worldpatch.jsonl`, at t=2274 (item 58, crafting table) and t=2320
  (item 269, wooden shovel, slot 6). Before t=2320 magma holds nothing and can
  only draw a bare arm, which is wrong wherever the oracle holds something; the
  first golden after the re-anchor is t=2440. So the hand window is a property
  of the sidecar, not of the oracle, and it moves if the sidecar is extended.
  **t=1140..1220 is a BARE ARM, not a held block, and this entry said otherwise
  twice.** The yaw-sweep test was right that the corner object is a screen-fixed
  viewmodel; the identification of it as a held block was wrong. Opening the
  lower-right crop of the goldens at t=0, 900, 1140, 1800, 2400, 2800 and 3100
  shows the same skin-toned tapered wedge in the same screen position over
  seven completely different backgrounds (water, grass, stone, planks). That is
  `ItemRenderer.renderArmFirstPerson` with an empty main hand, and magma has
  had that path since the HAND agent landed it.
  **A discriminator that reads backwards, so do not reuse it:** the fraction of
  that box holding wood/skin colour is constant to three decimals (0.747 /
  0.748 / 0.747 / 0.747 / 0.747) over t=1140..1220 and swings wildly (0.089 /
  0.000 / 0.000 / 0.436 / 0.005) over the confirmed hand window t=2440..2520.
  The constancy is real and the reading taken from it was not: it says
  "screen-fixed", which a bare arm and a held block satisfy equally. Compare
  the two frames at 2x and look, rather than scoring a colour fraction.
  **A gate hole this exposed:** `hide_gui`/`hide_hand` drop the POSITIONAL
  viewmodel barrier, but `pixel_gate` also has a post-hoc semantic `viewmodel`
  class ("held-item region: lower-right, touching a frame edge") with a 40000 px
  budget, and the same `hud` heuristic (`y0 >= h*HUD_FRAC`) shadowed the bottom
  rows, so un-masking those regions did not actually put them under measurement
  on any frame where a heuristic fired - the 17969 px at t=1180 were classed
  `viewmodel` and never counted. Both semantic classes now honour
  `hide_gui`/`hide_hand`. On this tape `hud` disappears entirely (107 frames /
  244695 px of it were never an explanation), `viewmodel` drops to the ticks
  from 2440 where there really is a hand (63 frames / 461603 px -> 35 / 94657),
  and failed frames go **18 -> 28**. That is the price of measuring the last
  unmeasured quarter of the frame, and every one of the new failures is in it.
  No other tape sets `capture.hide_gui`, so nothing else moves.
  With the gate honest, the viewmodel is the single largest remaining
  divergence on this tape: **11 of the 20 cluster-failing frames** are
  dominated by one cluster in that corner, in two silhouettes - 30064-30503 px
  at y[349,479] x[559,853] over t=600..660, and 17076-18406 px at y[335,479]
  x[601,771] over t=700 and t=1200..1340.
  **It is NOT recorder-blocked, and an earlier revision of this entry filed it
  that way.** The oracle is empty-handed for most of the tape, so there is no
  inventory to miss; magma's arm geometry is already right. What is wrong is
  the arm's SHADING. Forcing the hand on for the whole tape
  (`MAGMA_HAND_FROM_TICK=0`, which now overrides the sidecar) and comparing the
  gate-independent per-tick `whole mean/ch` against the suppressed run: **110
  of 157 frames get worse, 10 better, 37 unchanged** (mean 5.91 -> 6.92). The
  arm is drawn far too bright. On the two cleanest frames, where terrain and
  sky agree to within 1/255:

  | tick | probe | golden | magma | ratio |
  |---|---|---|---|---|
  | 900  | arm (639,429)     | (139,103,84) | (192,173,148) | 0.72 / 0.60 / 0.58 |
  | 900  | terrain (639,299) | (87,114,69)  | (87,114,70)   | exact |
  | 900  | sky (299,59)      | (155,188,255)| (154,190,255) | exact |
  | 1140 | arm (639,429)     | (146,107,88) | (201,180,154) | 0.73 / 0.60 / 0.58 |
  | 1140 | terrain (640,301) | (152,147,103)| (152,147,104) | exact |

  **Half of that was a SKIN MISMATCH, and reading the ratio as "per-channel, so
  a lightmap colour" was wrong.** The tape header has no `skin` field, so
  `replay_tape.py` fell back to slim and magma drew ALEX against the oracle's
  Steve. The tape's own `qrl_launch.determinism.pin_skin` is true, and
  `MixinRandomSkinTexture` forces the classic model whenever it is set;
  `tape_skin()` now honours it (`db5ac63`). Against the Steve texel (150,111,91)
  the golden is a clean scalar 0.660/0.658/0.659 - it was never a coloured
  multiplier, it was a paler texture. With Steve drawn, forcing the hand on for
  the whole tape flips the A/B: **91 of 157 frames better, 29 worse, 37
  unchanged, mean whole/ch 5.91 -> 5.07** (t=900 7.44 -> 6.17, t=1140 4.69 ->
  1.38, t=2300 2.50 -> 1.15). The suppression is still on because the residual
  is unfixed, not because the arm is a net loss; flipping the sidecar is a
  release-time call since it re-baselines the tape.
  **There is no "scalar ~1.57x over-bright arm" residual. That entry was wrong
  and is retracted (2026-07-25).** It came from dividing the golden by a raw
  atlas texel, which prices in the shading the oracle also applies. Measured
  against the actual magma render on the actual arm pixels, the arm is already
  right. Method: replay the tape twice, once with `MAGMA_HAND_FROM_TICK=0` and
  once with it past the end, and take the arm mask as the pixels where the two
  differ by more than 3 (eroded 2x to drop the silhouette); then read
  golden/magma over that mask. Every rain-free frame from t=0 to t=1780 comes
  back **0.997 / 0.995 / 0.995**, on 16796 arm pixels, across seven distinct
  yaw/pitch poses (yaw 0 to -390, pitch -45 to +90). `hand_diffuse` and
  `build_arm_matrix` need no further work; do not go looking at the eye-space
  normals, which is what the retracted entry sent the last agent to do.
  What is actually left on the arm is two things, neither a hand bug:
  - **The rain window t=1800..1980**, where the arm ratio is a flat achromatic
    **0.670 / 0.668 / 0.672** while sky (0.787/0.805/0.827) and terrain
    (0.758/0.774/0.694) are chromatic and milder. The arm is the pure lightmap
    readout - it takes no fog blend - so it shows the whole error. See "The
    lightmap ignores rain and thunder" below; the arm is just the cleanest
    place to measure it.
  - t=2020..2200 (0.836/0.904/0.920) and the wild ratios at t=2280/2360/2680/
    2760, which are frames where magma holds a different item, or none - the
    `worldpatch.jsonl` inventory re-anchor gap already filed above.
  Because the arm is exact outside those two windows, **flipping the sidecar's
  `hand_from_tick` to 0 is now the better default** and no longer trades a
  known-wrong brightness for a position win. It still re-baselines the tape, so
  it stays a release-time call.
  **Pickup inference cannot rescue the held-item intervals, so do not try it.**
  The idea was to derive `set_inventory` rows from `EntityItem`s that vanish
  near the player. The tape does carry 8673 EntityItem rows, 530 of them within
  three blocks of the player, but every one is **7 fields**
  (`id, name, x, y, z, yaw, pitch`) with no item id. The worldpatch is no help
  either - all 1317 of its `set_block` rows are at tick 1, an initial-world
  snapshot rather than an edit log. Recovering WHICH item needs the tape
  re-recorded with the every-20-tick inventory keyframes the recorder now
  emits; recovering the ARM does not.
  **t=540..660 is a held DIRT BLOCK, and it is inferable from the tape.** These
  are the only 10 ticks the forced-hand A/B improves, because magma's pale arm
  is closer to a brown block than the grass it draws with no hand at all. The
  player is looking straight down (`pitch` exactly 90.0 across the window) and
  the corner is filled by a dirt-textured object that is pixel-identical at
  t=600 / 620 / 660 while the grass at its edges shifts. A whole-corner region
  test says "terrain" here and is wrong - region `[350:480, 560:854]` tracks the
  background (mean |d| 0.85 / 10.98 / 11.93 against a control of 0.96 / 15.22 /
  14.13) only because the viewmodel is a small part of it. The 12x14 patch at
  the arm's own location is identical to 0.1 across all four ticks. Take the
  measurement on the object, not on a region that mostly is not the object, and
  then look at it at 3x.
  The inputs say the same thing: `atk` at t=560, `use` at t=680, i.e. mine,
  hold, place. That is the one held-item interval on this tape whose identity is
  recoverable without re-recording - not from `EntityItem` (no item id) but from
  the block that was mined, by ray-casting the recorded eye/yaw/pitch at t=480
  (the mine runs t=480..562 at `pitch` 15, the place t=611..680 at `pitch` 90
  with `y` climbing 71 -> 73, i.e. pillaring up with what was just dug). Doing
  it needs magma's generated world, not just the sidecar: the worldpatch is a
  sparse PATCH of 1317 cells, not a full snapshot, and a raycast against it
  alone hits nothing. Nobody has tried it. The
  goldens over that window also draw the block selection outline, which is
  worth checking against magma separately.
  **A wrong reading to not repeat:** the t=1800..1960 window first looked like
  a missing first-person held item - the oracle shows a dark held log and
  magma appeared to show none. It is not. Golden/candidate over that window is
  a uniform ~0.45 ratio across the whole frame including the sky (t=1900 whole
  0.726, sky 0.809, ground 0.696), i.e. the already-filed oracle rain
  darkening; magma's log is simply drawn at full brightness against grass and
  reads as absent at a glance. Zoom before concluding.
  The other canonical tape, `20260721T215812Z`, has a HUD on all 181 goldens
  and is unaffected; do not set `capture.hide_gui` on it. Its own 7 failed
  frames are all ONE family and it is the texel-selection residual above, not
  anything tape-specific: t=260 (7291 px) and t=460 (6252 px) fail on
  `texel-selection` clusters at sel 0.55 / 0.50 on a distant canopy and a
  grazing leaf underside, where both sides draw the same leaves from the same
  palette in a shuffled arrangement plus a 1-2 px sky/leaf silhouette edge.
  t=300 / t=320 / t=700 have no UNEXPLAINED cluster over 300 px at all and fail
  purely on mild-shift, i.e. the same wash spread thin. Closing the texel
  residual closes this tape.
- `scenario_slime_bounce_20260723T001527Z` (tape retired 2026-07-30,
  superseded by `20260730T095754Z`; this shell contradiction is the ONLY
  remaining slime residual and every failed frame on the new tape is
  0-unexplained global-check, i.e. exactly this): the slime platform renders
  too dark. Baseline on the old tape: **15 failed frames, 6709 UNEXPLAINED px**.
  `models/block/slime.json` (1.11.2 jar) has TWO elements - inset core
  `[3,3,3]..[13,13,13]` and full cube - both without cullface.
  `BlockSlime.getBlockLayer` is TRANSLUCENT. Emitting the real inset core
  (`5da6b29`) took 19 -> 16 failed frames.
  **Per-pixel arithmetic (t=50, face ROI y[300,360], a=188/255 from slime.png):**
  solve `g = C·(1-(1-a)^2) + B·(1-a)^2`, `c = C·a + B·(1-a)` on dark residual
  pixels gives C≈tex mean [120.7,200.0,101.1] and B≈0. Golden matches dual-layer
  over black; magma matches single-layer. On dual-covered pixels (block
  centers) magma already equals golden, so `raster_cpu` SRC_ALPHA
  (`c·a + d·(1-a)`, blend=1, no depth write) is correct when both layers hit.
  **Coverage map:** residual is a block-scale checkerboard - bright dual centers
  (core XY [3,13]^2) vs dark single rims (the 3/16 XZ frame where only the
  outer top draws). Golden is uniform dual brightness across the whole face.
  Rim fraction of a top face is `1-(10/16)^2 ≈ 61%`, which matches the bulk of
  the dark residual.
  **Levers tried (2026-07-25, wt/slime2), all rejected or insufficient:**
  - Full generalQuads (no neighbor cull on both elements): 15 -> 17 failed,
    darker (confirms prior -93/ch overshoot). Vanilla draws those quads
    (`BlockModelRenderer.java:105-110`) with GL cull + `sortVertexData`, but
    magma still overshoots when given the same layer count.
  - Translucent B2F sort alone (painter's order on 6-vert quads): 15 -> 14
    failed on this tape, but nightly REGRESSION on `elytra_dip` UNEXPLAINED
    784 -> 16495. Not landed.
  - Always-emit all 6 core faces (outer still culled): no further gain; core
    sides do not fill the rim to dual-top brightness (side shade 0.8 stacks to
    ~0.78·C, dual top is ~0.93·C).
  - Coplanar outer re-emit: 12 failed / 4.03 (prior), fakes uniform dual
    coverage; not the model; not landed.
  **Source and ordering closure (2026-07-27, wt/slimerim):**
  the adjacent-slime cull hypothesis is ruled out. `ModelBakery.java:685-698`
  puts every face whose JSON `cullface` is null into `generalQuads`; therefore
  slime's two six-face elements produce 12 general quads and zero per-facing
  quads. `BlockModelRenderer.java:93-110` calls `shouldSideBeRendered` only for
  per-facing lists and renders the null-facing list unconditionally.
  `BlockBreakable.java:37-55` does suppress a face against the same block type,
  but that method is never consulted for these 12 quads. Shared faces must
  therefore be present in the vanilla buffer.

  The other two state hypotheses are also source-closed. Vanilla sorts
  TRANSLUCENT quads by descending squared centroid distance within each
  16-high `RenderChunk` (`RenderChunk.java:339-344`,
  `VertexBuffer.java:69-92`) and renders the layer with depth writes disabled
  (`EntityRenderer.java:1448-1466`). Magma already uses the same SRC_ALPHA
  blend without a translucent depth write (`cpu/raster_cpu.c:209-219`), so a
  first translucent face cannot selectively occlude the inset core through
  the depth buffer.

  A new combination experiment tested the two faithful source consequences
  together rather than repeating either rejected lever alone: emit all 12
  general quads for every slime and partition the C column mesh into vanilla
  16-high sections, sorting each section's six-vertex quads by the same
  descending centroid-distance key. It regressed **15 -> 17 failed frames**;
  UNEXPLAINED stayed **6709 px**, and t=50 whole/terrain error increased from
  **7.21/8.00 to 7.93/8.83 per channel**. Reversing both section and quad order
  was a direction diagnostic, not a proposed fix: it failed much harder at
  **19 frames, 73321 UNEXPLAINED px**, with t=50 whole/terrain
  **34.80/35.74**. Neither change is landed.

  The t=50 arithmetic explains why ordering looked plausible but also refutes
  it as the missing implementation lever. On 30205 selected dark-rim pixels,
  golden/candidate medians are `[113,185,95]` / `[91,148,76]`; on 2061
  already-dual pixels both medians are `[114,185,96]`. With
  `a=188/255`, a dual top has coefficient
  `1-(1-a)^2 = 0.930965`. A top plus two 0.8-shaded internal N/S faces behind
  has coefficient `a + (1-a)*0.8*(1-(1-a)^2) = 0.932940`, only
  `[0.24,0.40,0.20]` RGB above dual at the texture mean. That numerical match
  does not survive the actual source-defined quad population and order.

  **Gate-accounting correction:** in the current baseline, `pxdiff clusters`
  assigns the large dark platform clusters at t=50 to the semantic
  `particles`, `viewmodel`, and `hud` masks. The reported 6709 UNEXPLAINED
  pixels are the separate horizon-edge family, so a rim-only correction cannot
  make that counter approach its alleged ~750 floor without changing
  classification. The next non-fudged experiment needs a live oracle
  translucent draw capture (quad buffer plus post-transform fragment order),
  not another inferred cull/sort/depth variant.
  Open gap: how vanilla keeps the rim as bright as dual-top without the
  overshoot magma hits when fed full generalQuads. Blend equation itself is
  not the bug on dual-covered pixels.
- scenario_elytra_dip triage bullet: CLOSED 2026-07-30: re-recorded tape 20260727T214459Z is rc 0 with pixels clean after the native-resolution flow-texture fix (d5ce4fe); old-tape forensics preserved. Full entry in CLOSED_DIVERGENCES.md.

- scenario_ender_dragon_20260722T093713Z triage bullet (stale tape): CLOSED: tape superseded by 094040Z (and 20260730T093740Z, both rc 0). Keeps the do-not-fix warning: never call gm_dragon_init from set_dimension; replay ghosts already render the recorded dragon. Full entry in CLOSED_DIVERGENCES.md.


### CPU/CUDA replay parity: closed, keep sweeping

CLOSED: CPU and CUDA replay outputs match; keep sweeping both backends after renderer merges (rebuild magma_game_cuda too, or the gate scores stale frames). Full entry in CLOSED_DIVERGENCES.md.

### Late-tape item acquisition on the canonical tape

Widening the inventory gate to every `inv`-bearing tick (rather than only the
every-20 sample grid) exposed two real divergences on
`20260721T215812Z`, which the old gate could not see:

- t=3257 slot 1: tape has item 270 (wooden pickaxe), magma has nothing.
- t=3267 slot 2: tape has item 50 (torch, count 8), magma has nothing.

Both are **crafted** items, and crafting/container clicks are not recorded in
tapes at all. The documented re-anchor for that is a `<tape>.worldpatch.jsonl`
sidecar carrying `set_inventory` events; `20260712T055346Z` has one, the
canonical `20260721T215812Z` does not. So this is an instance of the known
recorder blocker ("Legacy GUI interactions and inventory contents were not
fully recorded"), not a magma simulation bug - but it was invisible until the
gate started checking off-grid inv ticks. Fix is to author the sidecar from the
oracle session save, or to re-record with GUI interactions taped.

The tape's exit code is still 3 (its long-standing pixel FAIL short-circuits
before the state check), so the run now prints an explicit
`[gate] NOTE: inventory state ALSO failed` and `gate_baseline_diff.py`
compares the state block.

### The world diverges from the oracle's on the canonical tape

`20260712T055346Z` reports `world nearby_hash` deltas on **96 of its 157**
sampled ticks. The hashes agree through t=300 and separate from t=400 onward,
which is where the mission starts digging. Block state is not compared by the
replay gate (only physics, inventory and pixels), so this has been sitting
under the pixel numbers rather than being reported as itself.

It accounts for the tape's worst remaining frames, t=2840..2900: 47846-48202
px, `mean_delta [-41.69, -39.60, -38.63]`, an achromatic ~40-level darkening
over a third of the frame. Side by side at 2x it is block content, not shading:
both sides are in the same mined tunnel and draw the same held shovel, but
magma has a large flat near face filling the left of the view where the golden
sees a lit tunnel receding. One side has a block the other does not. The player
holds `atk=1` continuously through that stretch. The gate classes both frames
as `particles` and only flags them because they blow the 40000 px class budget;
an achromatic darkening over a third of the frame is not particles, so the
class is wrong even though the failure is real.

The cause is the recorder, not magma's simulation: dig progress depends on the
held tool and on GUI/inventory interactions that tapes do not record (see the
recorder blocker and the `worldpatch.jsonl` re-anchor above), so magma's dig
timing drifts from the oracle's and the two worlds part company. Chasing these
frames as rendering bugs is wasted effort until either the world is re-anchored
or the tape is re-recorded with block edits taped.

### Inventory gate coverage is still uneven

The gate now reports `ticks_independent` and flags `seeded_only`, but 13 of 23
tapes still carry only the tick-0 `inv` row and so verify nothing beyond the
seed. The recorder emits an inventory keyframe every 20 ticks as of this
change; the tapes have to be **re-recorded** before that takes effect. Count
and metadata are also still not compared - only item identity per slot - so
arrow-count drift and durability ticking remain ungated.

### Truncated tapes verify only a prefix

**Seven** tapes stop at a terminal death and only ever replay part of their
length. Four of them exit rc=0, so nightly counted them fully green while
verifying less than half:

| tape | replayed | of | % |
|---|---|---|---|
| `smoke_zombie` | 358 | 803 | 45 |
| `ender_dragon_demo` | 596 | 1614 | 37 |
| `ender_dragon_094040` | 607 | 1610 | 38 |
| `wither_skeleton` | 610 | 1202 | 51 |
| `enderman_fight` | 666 | 1402 | 48 |
| `blaze_bow_demo` | 814 | 1407 | 58 |
| `blaze_melee` | 999 | 1203 | 83 |

The deaths are **correct** - the oracle dies
at those ticks too and respawns (canonical check: tape tick 813 `hp=0.0`, tick
814 `hp=20.0`) - and `continue_after_death` is deliberately emitted only for
fluid episodes (`replay_tape.py`, `game/script.c` script loop), with a test
pinning that contract.

The problem was that nothing recorded the truncation, so a tape verifying 38%
of itself reported clean. The state gate now carries a `coverage` block and the
replay prints `[tape] COVERAGE: only N of M tape ticks were replayed`. Whether
to extend the contract past respawn (emit `continue_after_death` for any
`tape_has_respawn`, teach `first_divergence` to resume, update the pinning
tests) is an open product decision, not a bug.

### slime_bounce horizon band: NOT a render-distance cull mismatch

CLOSED 2026-07-30 (negative result retained): the band was never a cull mismatch; root cause was camera eye height. Full entry in CLOSED_DIVERGENCES.md.

### slime_bounce: horizon band CLOSED, shell contradiction isolated (2026-07-30)

CLOSED 2026-07-30: sneak eye height 0.08F (65ea82a) + duplicate offset removed in frame_capture. Residual = the shell inset contradiction, still OPEN in the triage section. Full entry in CLOSED_DIVERGENCES.md.

### slime_bounce horizon band: fog-blend decomposition (wt/horizonfog, 2026-07-27)

CLOSED 2026-07-30 with the horizon family (sneak eye height); full decomposition preserved for reference. Full entry in CLOSED_DIVERGENCES.md.

### fogColor1 non-convergence at recstart

CLOSED 2026-07-30: recorder writes fog_color1 into the tape header, replay seeds MAGMA_FOG_C1_INIT from it; all 2026-07-30 re-records carry the field (slime t=0 clean, elytra_dip header 0.99999976). Legacy tapes keep the steady-state seed. Full entry in CLOSED_DIVERGENCES.md.

### Rain/thunder lightmap attenuation is resolved; tape promotion remains open

`frame_capture.c` now feeds the live interpolated rain and thunder strengths
through vanilla's two `5/16` sun-brightness factors before building the
lightmap, and the focused weather render/native gates cover the active path.
The old hardcoded-clear renderer diagnosis below is therefore closed.

The canonical Java pixel-tape claim is still open for a separate recorder
reason: legacy rows do not carry per-tick rain/thunder strengths. New headers
record the initial strengths, but a window beginning mid-transition needs the
row state or a fully verified timer replay. Re-record the weather tape before
promoting its arm/terrain residual; do not fit to the old `known:12` class.

### Remaining isolated render features

- One-frame loading sky after a dimension transfer.
- Enchantment glint.
- Chest model rendering and world-layout seed parity.
- Arrow ghost pitch on legacy tapes.
- General held-item registration outside the pinned use poses. (The rim
  edge-shading half closed 2026-07-30: generated-item rim quads now invert
  WEST/EAST normals per Forge ItemLayerModel.)
- Sheep grazing/head pose is only partially matched
  (scenario_sheep_grazing_20260730T092648Z replays rc 0, so the residual sits
  under gate thresholds; not re-measured pose-by-pose).
- Dig particles are reconstructed but not pixel-perfect.

## Simulation and replay

- State-capsule version 2 is exact for its declared bounded subset, including
  all 41 player inventory slots with the promoted enchantment representation,
  XP/hidden total, combat timers, absorption, ordered potion effects, the full
  isolated weather/daylight clock, and optional saved skylight. The permanent
  mixed continuation matches 27 state categories and every one of 10,625 raw
  block, block-light, and skylight cells for 20 ticks. This does not promote
  arbitrary ItemStack NBT, general entity/task NBT, arbitrary tile entities or
  scheduled ticks, loaded-chunk random-tick membership, or weather-driven
  loaded-chunk side effects.
- Outer-End population matches the injected Java RNG cursor, but automatic
  live discovery still approximates the provider cursor from southeast-neighbor
  state; arbitrary chunk-load order and reload persistence are not promoted.
- End cities/ships and treasure are live, but shulker entity behavior and
  chorus-plant internal-face pixel culling remain open.
- Village recursive graphs and live blocks, farms, doors, blacksmith loot, and
  resident spawn-site/profession records are exact in the focused fixtures.
  Ordinary residents materialize once and render profession models. The initial
  ordinary trade slice is live and Java-locked across 11 career selections and
  22 offers. An unopened NoAI villager also round-trips through the shared state
  capsule and continues for 20 exact ticks, including profession, age, lazy
  economy state, living-sound timer, and private RNG/Gaussian state; the first
  lazy trade after reload remains exact. Villager AI, reputation/doors,
  breeding, golems, initialized economy persistence, enchanted/map offers,
  later tiers/restocking, merchant UI, arbitrary load-order persistence, and
  pixel promotion remain open.
- Fishing gameplay, nested loot, and the 17-point first-person line centerline
  are live; partial-tick hook endpoint interpolation, final line pixel
  promotion, exhaustive hooked-entity edges, and bobber/custom-NBT persistence
  remain open.
- Firework gameplay is live; custom explosion-NBT rendering and particle pixel
  parity remain open.
- Weather lightning/gameplay and accumulation callbacks are live; thunder and
  impact events enter the live audio stream. Precipitation loops, particles,
  bolt pixel parity, and the canonical weather tape remain open.
- Entity-driven world edits such as crystal-explosion fire do not replay.
- Dragon ring-buffer cold start and unload reset differ.
- Some end-to-end Oracle runs lose the dragon boss-bar registration.
- Mob roster, AI/spawn details, and boat `UNDER_WATER` state remain incomplete.
- Aim-pin target changes can add a one-tick block-break lag.
- Hotbar arrow count can drift while the Oracle shoots.
- HUD heart-flash blinking is not modeled.

## Oracle, recorder, and world-state blockers

These are not established C product bugs, but they block direct parity claims:

- Tick 9811 Oracle save-state contains flowing water absent from pristine
  worldgen.
- Live-session population order can change individual decorations.
- Legacy GUI interactions and inventory contents were not fully recorded.
- Legacy tape headers omit `EntityRenderer.fogColor1` and its pre-capture
  brightness history. On `scenario_suffocate_camera_20260723T001923Z`, the
  actual in-block overlay frames pass, but t=0 is a frame-wide shading offset
  that decays from 7.69 mean/ch at t=0 to 2.41 at t=10 and 1.16 at t=20.
  Vanilla carries this 0.1/tick smoother in
  `EntityRenderer.updateRenderer`; reconstructing its initial value from the
  golden would be fitting an unrecorded constant.
- Walking/turning tapes retain partial-tick camera registration uncertainty.
- Legacy `EntityItem` rows omit required render state.
- The mine segment contains a mid-tape staged arena-state window.

When one of these is encountered, improve the recorder/pin or classify the
capture as blocked. Do not fit C output to an unproven Oracle state.

### Recorder gaps proven 2026-07-30 (overnight flywheel)

1. Explosion particle clouds are unreproducible from any tape:
   `doExplosionB` particle spawns consume client `world.rand`, and the tape
   records neither RNG state nor particle instances. A substitute-seed
   reconstruction visibly differs and fails the gate (creeperpix delegate,
   fix_creeperpix findings). Physics through explosions IS exact via the new
   `expl` packet capture (MixinRecordExplosion, additive knockback + block
   clears in replay). Next recorder step: whitelist particle-instance capture
   in `ParticleManager.addEffect` for explosion classes; this also makes the
   dragon-death white puffs deterministic. Affected today: tnt_explosion
   (t=30 blast cloud, partly classed UNEXPLAINED because the particles
   brightness heuristic misses cloud edges), creeper_encounter.
2. FIXED 2026-08-02 (recorder opt-in): elytra flag-7 arming round-trip varies
   per recording - a 2-tick `elytra_flying_pending` model makes
   scenario_nether_elytra_20260729T110024Z physics-exact (351/351) but breaks
   scenario_elytra_dip_20260727T214459Z at tick 59, and the 1-tick model does
   the reverse, because the CPacketEntityAction -> metadata round trip on the
   integrated server is not a constant. Fix: MixinRecordPlayerMetadata records
   the observed SPacketEntityMetadata flag-7 arrivals (`flag7_metadata`/`f7`),
   replay forwards them as `set_elytra_flag7`, and the same recording round
   also captures the pre-travel rotation (`look_phase`/`ry`/`rp` at
   ClientTickEvent.START -> `set_look_pre`) since WHICH side of travel a look
   change lands on also varies per recording (nether_elytra armed with the
   new pitch; the fresh-world t371 walking turn used the old yaw).
   scenario_elytra_dip_20260803T032614Z: physics-exact 520/520 at 1e-9, all
   state gates pass, pixel rc=0. scenario_nether_elytra_20260803T033526Z:
   physics fully clean (incl. hp) through terminal death at t=110, java-mode
   world hash 0 mismatches; its pixel FAIL is items 5/9 debt, and takes that
   wall-crash earlier instead hit item 17. Legacy tapes byte-identical
   (sha-proof on both old elytra tapes).
3. Tape headers record no gamerule state. silverfish_encounter runs
   `naturalRegeneration false`, replay simulates the vanilla default, and hp
   diverges 0.4 at t49 (= 1.0 silverfish damage x Resistance III 40%
   residual: the damage amount itself is exact), then food/exhaustion drifts
   at t361. Suppressing only regeneration in an instrumented replay removes
   both. Recorder fix: serialize gamerules at recstart; replay consumes them.
   Affects any scenario relying on non-default gamerules for survival stats.
4. falling_blocks records sky-only goldens deterministically (4 takes,
   including phased tp-first staging and a 400-tick settle): selection
   wireframe and hotbar render, so the client HAS block data, but chunk
   meshes never build during this specific scenario while same-session
   neighbors record fine. Needs live oracle debugging; all four takes retired
   from the sweep.
5. The nether_elytra world snapshot lacks transient lavafall cells
   (x=-123, z=-86, y=32..46); a counterfactual fill reproduces the golden.
   Needs dynamic-fluid snapshot capture; re-recording alone will not fix it.
6. Magma has no gravity-block cascade: sand/gravel never convert to
   FallingBlock entities on support removal, so the world blocks diverge
   from the first conversion tick while render stays plausible (tape ghost
   views draw Java's falling entities over magma's still-floating column).
   Caught 2026-08-01 by the Java/C world digest gate on the first VALID
   falling_blocks take (scenario_falling_blocks_20260801T151855Z): ticks
   0-19 digest-identical, dig applies one tick late (Java t20, magma t21,
   same digest value - separate small skew), first cascade divergence t22,
   magma world frozen from t30 while Java evolves through t59. Fix needs a
   real falling-block sim (convert on support removal + neighbor updates +
   landing re-block); the 1-tick dig skew deserves its own look at the
   attack-input replay alignment.

Status updates 2026-08-01: item 3's recorder half is DONE (recstart now
serializes all gamerules into the tape header); replay-side consumption is
still open. Item 4 no longer reproduces: the 2026-08-01 falling_blocks
takes record real terrain goldens (chunk meshes present), so the scenario
is recordable again - its gate now fails honestly on item 6 instead.

Status updates 2026-08-01 (evening wave): item 3 is CLOSED end-to-end -
replay consumes header gamerules (naturalRegeneration/doDaylightCycle/
doWeatherCycle honored, rest deliberately inert), and the recorder now
reads the INTEGRATED SERVER's rules instead of the join-time client copy
(only doDaylightCycle ever synced client-side, via SPacketTimeUpdate's
negated worldTime - which is why it alone recorded correctly). Verified:
silverfish_encounter 175112Z records naturalRegeneration=false truthfully
and replays physics-clean where 172741Z diverged at t101. Item 6 is
LANDED: vanilla gravity-block cascade (BlockFalling delay-2 scheduling,
EntityFallingBlock motion/landing, cascade notifications) plus creative
GameType propagation into the dig controller; falling_blocks 151855Z world
digest now matches 309/310 ticks. Residual: single-tick t46 mismatch from
the blockHitDelay/packet-order boundary (client observes re-landed sand
and its held-creative removal in the same post-tick state; magma splits
them across t46/t47). The 1-tick dig skew half of item 6 is absorbed by
the same change (dig lands at Java's t20).

Pixel triage 2026-08-01 (full report:
~/dev/nw/pxtriage_reports/pxtriage_20260801.md, covering the
three state-clean rc=3 takes) yields four new tracked items:

7. Dragon fireball billboard renders too bright/saturated vs the oracle's
   muted purple (silhouette also differs). Path: entity_render.c
   billboard for EntityDragonFireball -> item 9003 ->
   emit_fireball_billboard() forcing light=1/blk=15/white tint. Confirmed
   by eyeball on dragon_kill_geared 175614Z t=181..224 (17 frames, 1897
   px). Needs an atlas-vs-lighting A/B to pick the exact fix.
8. Recorder gap: entity rows lack prevPosX/Y/Z and limbSwing state, so
   Java's interpolated render pose cannot be reconstructed for close-range
   articulated mobs (silverfish 175112Z t=260, 15704 px across 10 frames;
   ghost STATE matches exactly, only render pose diverges).
9. Recorder gap: one-tick death-screen transition state (death timer,
   HUD/hurt/game-over in-frame ordering) is not captured; water_dive
   173755Z t=990 trips the global mild_shift detector with zero
   unexplained clusters. Cosmetic-only.
10. Oracle capture-loop artifact: dragon_kill_geared 175614Z golden frame
    hashes REPEAT (t=14==t=32, t=16==t=34, one frame spans t=22..30 and
    again t=40..50, recovering at t=52). Take-level recording artifact,
    not a magma defect; re-record if this take is ever promoted.

The dragon death-cloud unexplained px (17175 across t=266..474) are the
pcl-consumption gap: the tape carries 1179 pcl spawns that replay does
not yet consume (work in flight); take-variable until then.

Interactive-play sweep 2026-08-01 (first human session on the Mac Metal
windowed build; every item below is from the interactive path that no
pixel gate renders - the windowed-path blindspot class):

11. FIXED: cutout draw buffer overflow abort at seed-0 plains spawn, vd8
    (262,932 verts > 262,144 cap; tall grass). Cap doubled to 524,288;
    measured peak documented in magma.conf. Tape peaks never passed 33K
    because no pinned tape renders a plains spawn at rd8.
12. FIXED: live sheep/pig/cow/chicken now run their vanilla 1.11.2
    priority/mutex task lists for swim, panic, eat-grass (sheep),
    wander-avoid-water, watch-closest player, and look-idle. Movement and
    look helpers carry the task outputs into motion and rendered head pose;
    sheep panic uses 1.25 * the 0.23 movement attribute. Mate, tempt, and
    follow-parent remain dormant because live play has no breeding state.
13. Entities x-ray through translucent water: window compose draws all
    four terrain layers (window_compose.c render order solid, cutmip,
    cutout, trans) THEN entities, so entities paint over water. Vanilla
    draws entities before the translucent pass. Check frame_capture's
    order and match it; suspect capture path differs (water_dive pin is
    rc=0) or its scenes never put an entity behind water.
14. NOT A DIVERGENCE (source-verified): sand placed directly above tall
    grass stays put in vanilla 1.11.2. BlockFalling.canFallThrough is
    fire/air/water/lava materials only; tall grass is Material.VINE
    (BlockTallGrass.java:34), so updateTick's fall condition fails. The
    fall-through-replaceable-plants rule is a later-version behavior.
    Open sub-case to verify: a falling sand ENTITY landing INTO a tall
    grass cell must replace the grass (EntityFallingBlock landing
    setBlockState); qrl-record if touched.
15. FIXED 2026-08-01: dropped block mini-cubes used one `IR_CUV` map for all
    faces, bypassing the placed-block FaceBakery corner order and 0.999/0.001
    inset. The EntityItem cube path now consumes `rk_facebakery_make_quad`
    output directly. The registry census covers 49 cube item ids / 769 states /
    4,614 faces; every dropped UV is bit-identical to a fresh placed-face bake.

16. OPEN 2026-08-01: world spawn selection is not parity-verified. Seed 1000:
    Java spawns the player at (159.5, 56, 242.5); magma's interactive default
    starts at (8.5, 70, 0.3). Every pinned tape uses scripted set_pose, so no
    gate exercises vanilla's WorldProvider spawn search (grass-block scan +
    radius walk). Census cell; needs an oracle-vs-magma spawn-coordinate probe
    across a seed sweep before any tape relies on unscripted spawn.

17. OPEN 2026-08-02: elytra fly-into-wall kinetic damage is server-
    authoritative in both tick and amount, so magma's locally computed hit
    diverges. scenario_nether_elytra_20260803T032911Z: client collision at
    tick 70 (vx -1.2091 -> 0), magma applies speed*10-3 = 9.091 immediately;
    the oracle's recorded hp drop lands at row 72 (SPacketUpdateHealth round
    trip) and is 10.21 - implying the SERVER's own tracked speed (~1.321),
    not the client's. The 1.12-hp gap decided a death: magma hit 0 at t=101
    while the oracle died a tick earlier at t=100 with a different total.
    Fix shape is the flag-7 pattern again: record health-update packet
    arrivals (tick + value) and let replay apply recorded server damage
    instead of local kinetic computation - but hp is a gated physics field,
    so the design must keep the hp gate meaningful for locally simulated
    damage classes (fire, fall) while pinning only server-computed events.
    Filed, not implemented.

Micro-regression priced 2026-07-30: nether_elytra t=63 gained 2409
unexplained px (7 clusters, largest 1463) relative to its 2026-07-29
baseline after the night's renderer merges; physics still 351/351. Baseline
intentionally left old so the delta stays visible until attributed.

## Verification commands

```bash
make -C magma test-game
bash verify/ui_hud/run_ui_hud_gates.sh
bash verify/ui_entities/run_oracle_gate.sh
bash verify/mc_capture/run_gui_actions_verify.sh
bash verify/mc_capture/run_gui_verify.sh
```
