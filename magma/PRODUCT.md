# magma game product contract

Status: target contract, not a claim that the current binary implements every item.
Decided with the user on 2026-07-09.

## Current executable boundary

The shipped loop connects a fresh empty survival player to streamed default or superflat
terrain, movement/collision, dig/place/interact edits, natural item drops and pickup,
2x2/3x3 crafting, furnaces, route tools and durability, food/vitals, buckets and fluid
reactions, beds/explosions, armor and survival elytra, chests and stronghold loot,
dimension-owned route mobs, melee and projectiles, collectible XP orbs, linked Nether/End
travel, generated fortresses and strongholds, eyes of ender, End combat, the 200-tick dragon
death sequence, exit-portal entry, credits, and terminal `won`. Windowed and JSONL headless
execution share `gm_runtime_tick`; `--render off`, `--pace unlimited`, scripts, and state
output all use it.

`game/test_route_e2e.sh` is the macro acceptance gate. On seed 0 it starts with an empty
inventory and legally reaches `won`. It injects only travel pose/velocity and encounter
entity placement at verified generated landmarks. It never injects inventory, blocks,
dimensions, drops, health/death, portal activation, dragon state, or victory.

Interactive container screens exist in the windowed client and share the single click
path with headless play: E (or using a crafting table / furnace) opens the player
2x2, table 3x3, furnace, or single-chest screen; mouse clicks are vanilla `Container.slotClick`
(PICKUP / shift QUICK_MOVE / Q THROW with real drop entities) over the full 36-slot
inventory plus four armor slots, offhand, grids, result, furnace, and 27 chest slots,
at the vanilla GUI slot coordinates
(`game/container_live.c`, `game/screen.c`). The same clicks are a survival action in
JSONL (`inv_slot`/`inv_button`/`inv_type`), and observations expose cursor, grid, and
craft-result state. The screens render the real MC GUI art (the container panels,
`ascii.png` font with vanilla `FontRenderer` metrics, furnace flame/arrow progress,
flat 16x16 item icons plus the layered/tinted potion bottle, all from
`assets/build_gui_atlas.py`), and the crafting-table and
furnace, inventory, and single-chest screens are Java-pixel gated:
`verify/mc_capture/run_gui_verify.sh` diffs the panel region against
live-game captures (`capture_gui.sh`). Table, furnace, chest, and inventory
non-preview chrome are bit-exact gates (A/B noise near-zero required; no
margin budget) at the pinned 854x480/scale-2 profile. The inventory
player-model preview is a hard open ROI gate under `pin_preview_anim`
(ageInTicks=0, pose1 parked mouse + held-out pose2 on inv slot A): PASS only
when residual is at the J-vs-J noise floor (pixel-perfect / bit-exact). Any
preview residual is FAIL / open — never a circular PASS-FLOOR allowance.
`gui_preview_calibration.json` records measured residual only, not a pass budget.

Populated Furnace and Brewing Stand screens have deterministic real-client
fixtures. The oracle fixture asserts the live tile progress fields, Java A/B is
byte-exact, and native matches all 116,792 owned panel pixels for each screen
with maximum channel delta zero. This covers the item/count overlays, furnace
fuel and cook progress, brewing bubbles and progress, and the empty-potion
overlay tint. Arbitrary Potion NBT colors remain in the broader item-rendering
tail.
Furnace titles use the live tile custom name when present. Named-item
placement, Structure copies, Java/Anvil capsule restore, state export, and
native checkpoints preserve that name; unnamed furnaces retain the vanilla
`Furnace` title.
Breaking represented block inventories follows 1.11.2 `InventoryHelper`
exactly: one shared random offset triple, 10..30-item splits, EntityItem
constructor draws, Gaussian velocity overwrite, pickup delay, UUID/entity-ID
order, and complete represented ItemStack payloads. The helper's private
static seed and Gaussian cache survive real Java saves, neutral capsules, and
native checkpoints. The bounded live table covers the strict worst case of
189 entities from a full 27-slot inventory, with measured headroom and no
idle-tick throughput change.

The populated Crafting Table, Anvil, and Villager trade screens also use
deterministic real-client fixtures with strict live-state assertions. Java
computes the crafting result, anvil rename/cost/result, and selected trade
result through the actual containers. Every Java A/B pair is byte-exact, and
native matches all 116,792 owned pixels for each screen with maximum channel
delta zero. The Anvil path includes the active name-field texture and Java's
four-pass `Enchantment Cost` text. Exhaustive recipes, anvil combinations,
trades, hover/tooltips, and cursor/text-entry states remain broader gates.

The JSONL runner also exposes strictly typed test-only pose, velocity, time, weather,
block, inventory, and entity mutations. They execute before the shared tick and are not
available through normal survival input.

Remaining product gaps are not hidden by those gates: the inventory 3D
player-model preview is rendered and ROI-gated but is not claimed pixel-perfect
until bit-exact (see `run_gui_verify.sh` residual + calibration report),
block-items draw as flat texture tiles where vanilla renders mini 3D blocks, and
item ids outside the atlas table fall back to colored pips. Ender, ordinary,
trapped, single, and double chests now use live entity-texture/lid passes and
focused real-Java subject gates; the wooden set retains only two pinned
minified edge/texel ties above max-channel 25. Several
exact type-specific entity animations/particles remain simplified;
the `--villages on` development path now enables exact recursive village
pieces, roads, farms/crops, houses/doors, blacksmith loot, and retained
resident spawn sites/professions. Ordinary residents now materialize into the
live mob store, render profession-specific nine-part models, and expose 22
Java-locked initial ordinary offers across 11 tested career selections. It is
not yet a complete product bundle: villager AI, reputation/door state,
breeding, golems, later trade tiers/restocking, enchanted/map offers, merchant
state persistence, and exhaustive pixel promotion remain open. The populated
selected-offer Villager screen is strict pixel-exact. Ice-plains and cold-taiga
scattered features now place the owned 1.11.2 igloo templates with exact
rotation, surface/basement selection, shaft depth, block states, tile families,
deferred chest seed and loot, and resident positions/types. Natural basement
residents materialize once with exact represented motion, health, rotation,
fire, air, profession, persistence, ground, and conversion fields; the priest
retains its two saved offers. Zombie villagers use a distinct live type, the
exact eight-box 1.11.2 model, and all six profession skins. Active conversion
behavior, arbitrary template entity NBT/attributes, and a stable Java/native
igloo pixel scene remain open. Generated swamp-hut witch sites now materialize
once into the live mob store with the exact represented pre-first-tick base
state, persistence-required flag, distinct witch type, and Java-locked
initial-spawn RNG consumption. The cold claim scan is memoized and adds no
unchanged-tick population traversal. Its self-potion loop now makes the exact
water-breathing, fire-resistance, healing, and swiftness choices, carries the
32-tick drinking state and movement penalty, applies completion effects in the
correct tick phase, and emits the rare status-15 event. Its target-dependent
choice among Slowness, Poison, Weakness, and Harming is exact for current
target distance, motion, health, and active effects. The 60-tick ranged task,
visibility cursor, private projectile Gaussian spread, splash collision/effects,
and hostile throw sound run through the bounded live projectile/audio paths.
Drink, hurt, and death sounds now use the exact Witch event bits and HOSTILE
audio path. Player-credited lethal hits run the exact seven-entry Witch loot
table with Looting, zero-stack discard, item-constructor state, gamerule
gating, and a retained death window. At terminal tick 20, an ordinary
non-drinking player-credited Witch now emits the exact base-five XP split,
orb constructors, global cursors, same-tick update order, and particle RNG
tail. Represented ordinary Witches killed by drowning, falling anvils,
periodic fire, lava contact, block-collision fire contact, cactus contact,
mixed still-water/fire contact, rain-exposed fire contact, in-wall damage, or
represented stone/hay landing damage
follow the real no-credit branch: Looting-0 table drops, no held-potion
equipment drop, no terminal XP, and exact feedback/death/particle cursors.
In-wall damage uses the captured 1.11.2 `causesSuffocation` predicate, including
exact piston and slab state differences, shared with the player overlay.
The promoted landing boundary preserves fall-distance damage, hay's one-fifth
multiplier, hostile fall and support-block sounds, and the exact block-state
dust packet descriptor. Non-ridden ordinary living mobs now share the exact
non-sneaking slime bounce and low-speed walking damping path, with zombie,
sheep, and Witch raw travel state compared against Java. Ordinary nonlethal
small/big stone falls, hay-reduced falls, and Jump Boost II avoidance are
additionally promoted for zombie, zombie villager, skeleton, wither skeleton,
creeper, spider, cave spider, pigman, silverfish, sheep, pig, cow, and
villager. That boundary includes exact hostile/generic fall audio,
type-specific hurt feedback, support-block audio, health and immunity timers,
effect state, RNG cursors, landing dust, and creeper fuse advancement. Lethal
generic falls, Enderman's teleporting damage override, slime's generic fall
callback, particle pixels, sneaking/nonliving slime, and other block callbacks
remain outside this promotion.
Ordinary Witch ticks also consume the exact
base ambient-sound RNG branch and emit the hostile Witch ambient event with
Java pitch. Their water handling now reuses the verified normalized-current
kernel, so flowing water accelerates motion before later lava contact. A
non-first dry-to-water transition also emits the exact HOSTILE splash event,
motion-scaled volume, pitch, and 26-call entity-RNG particle stream. All 26
particle descriptors reach the bounded water-particle renderer in Java order;
constructor-private visual entropy remains deterministic rather than a claim
of Java pixel identity. Player-credited drinking deaths also include
the optional potion equipment drop and equipped XP bonus. Rendered
held-potion/status particles, other unpromoted non-player lethal sources,
general-mob suffocation, the remaining special and lethal landing behavior,
arbitrary equipment/task NBT, player-facing persistence, and witch pixels
remain open. Bounded NoAI Witch fresh-NBT continuation is promoted by the
shared living capsule gate.
Enchanting `on` now
enables exact bookshelf scanning, offer generation/reseeding, item/lapis slots,
XP/lapis costs, application, and book conversion in a playable table UI, while
complete anvil and enchanted-book combinations and glint remain open. The
populated Enchanting fixture locks the actual Java container's seed, bookshelf
power, offers, clues, generated runic names, slots, and render-boundary book
pose; native matches all 116,792 owned pixels exactly. The populated Anvil
rename/cost/result screen is also strict pixel-exact against the actual Java
container.
Weather `on` now enables exact timers/strength interpolation, rain/thunder sky,
fog, lightmap, celestial attenuation, Java-locked rain/snow geometry, and
open-sky rain extinguishing. It also enables lightning lifecycle/events/fire,
represented strikes and mob conversion, plus ice/snow/cauldron precipitation
callbacks. Lightning thunder/impact events feed the live audio stream;
precipitation loops/particles, lightning pixel fidelity, broader precipitation
edges, and Java pixel-tape promotion remain open. Brewing
`on` now enables live stands, recipes, drinkable effects, GUI, persistence, and
comparator state plus splash and lingering effects for players and mobs. Mobs
now retain, combine, age, and expose bounded status effects; regeneration,
poison, fire resistance, Speed, Slowness, Strength, Weakness, and Jump Boost
execute live, Resistance reduces represented incoming damage after hurt
immunity, Wither pulses through the ordinary death/drop path, Health Boost
changes the live cap, Absorption shields represented damage, and Levitation
uses the exact living travel transform. Represented living mobs also carry
reloadable air state: eye-in-water decrements it, Water Breathing holds it,
dry eyes reset it, and the exact 320-tick drown pulse consumes the vanilla
bubble RNG before represented damage/death. Invisibility suppresses the live
mob model and slime gel layer for the product's survival viewer, clears on
effect expiry, and leaves the independently rendered fire overlay intact. The
player's Night Vision now uses the exact duration/partial-tick warning
flicker, post-provider 256-entry lightmap normalization, and post-fogColor1
clear, terrain, water, and lava fog normalization in both headless capture and
the interactive product. Player Blindness now uses the exact duration fade,
void darkening, and linear sky/terrain fog ranges in both render paths; it also
blocks Ctrl and double-tap sprint starts without cancelling an already active
sprint. The represented direct player attack also applies the exact weapon-specific
cooldown period, base/enchantment/critical ordering, Blindness and movement
predicate, target armor, sword/tool/hoe durability wear, ordinary target
knockback, sprint and Knockback-enchantment impulses, attacker slowdown, and
sprint cancellation. Fire Aspect preignition, accepted extension, rejection
rollback, and cooked lethal loot are exact. Full-cooldown grounded sword
sweeps apply the Java walking predicate, target query, knockback, base damage,
and Sweeping Edge ratio. The six player attack sounds retain Java identity,
order, source, category, volume, and pitch. Fresh player hits on every
represented living target emit the exact hurt/death event before the player
follow-up, including private-RNG pitch, hostile/neutral category, cow's 0.4
volume, ghast's 10.0 volume, size-scaled slime/magma volume and small/big
identity, pigman pre-damage anger RNG, sprint order, sweep-neighbor order, and
resistant-window suppression. Critical, enchantment-critical,
sweep, and damage-indicator particles now retain Java identity, order, count,
source geometry, and raw spawn arguments in the bounded runtime stream. The
interactive renderer owns their vanilla textures, layers, motion, color,
lighting, and lifetimes. Java's unsaved wall-clock/global constructor entropy
is represented by a deterministic native visual stream rather than claimed as
pixel-exact. Player-thrown splash potions now retain Java's nearby-thrower
suppression, exact two-tick release countdown, returning-player segment hit,
direct factor 1.0, impact removal, and ordinary player damage immunity state.
Player and represented-mob instant damage now retain the nullable indirect
owner, player credit, revenge target, hurt immunity, knockback, sound RNG, and
exact health/motion state for splash impacts and lingering-cloud scans.
The live client receives the exact clamped, 1/8000-quantized tracked velocity
packet while the server retains its unquantized motion. Thorns and statistics
side effects are still open. The remaining mob effect behaviors and
multi-entity throwable ignore ordering remain open. Thrown potion stacks and
clouds retain bounded ordered custom effects, ambient/particle flags, custom
color, and arbitrary stack-tag NBT through the neutral capsule. Lingering clouds emit the
exact active/waiting particle cadence, disc positions, colors, and Java RNG
cursor into the fixed runtime stream; the interactive ParticleSpell renderer
uses deterministic native constructor entropy and is not yet pixel-exact. The
bounded potion/cloud scalar, kinematic, and custom payload state now
round-trips automatically through the Java authoritative snapshot and neutral
state capsule. Area-effect clouds also retain common `Entity` fire/air/portal,
flags, fall/water/first-update, previous/last position, and authoritative RNG
state. Normal and Forge `UpdateBlocked` continuation ticks are exact against
the real server while the client particle RNG remains independent. Nonempty
generic name/tag/capability/passenger and active portal-entry state is rejected
at this bounded boundary. The Java-pixel suite does not yet cover every required HUD/entity/particle state. The
instrumented seed-0 and seed-7 terrain gates and the Java Nether/End portal suite pass.
Brewing stands also participate in the live hopper network with exact top,
side, and bottom slot visibility, insertion validity, potion output, ordinary
ingredient rejection, glass-bottle container extraction, and same-tick blaze
fuel consumption.
Furnaces expose their exact top input, side fuel, and bottom output faces to
hoppers, including the bottom fuel-slot water-bucket exception. The live tick
uses the complete 51-row Java 1.11.2 smelting registry and all 65 fuels found
across the 392 initialized item rows. Recipe and fuel lookup are constant-time
shared CPU/CUDA switches; exact-meta fish/sponge/stone-brick cases, tool and
armor recycling, vanilla stack limits, lava-bucket return, and wet-sponge
water-bucket conversion are covered by strict registry and tick gates. Player
output takes use exact fractional XP rounding and construct split XP orbs from
the shared Math/entity/UUID cursors; hopper extraction awards no XP. The real
output-slot order is retained through per-item craft statistics, the rich
Forge smelt event, and prerequisite-gated `acquireIron`/`cookFish`
achievements. These counters and events survive native checkpoints, and the
statistics writer preserves every unrelated byte of the imported Java JSON.
Eight direct Java/native NBT-reload pairs cover every furnace tick branch
around burn, cook, output, fuel, and wet-sponge boundaries. Joined
ordinary chests use Java's canonical west/north half first for both insertion and
extraction, and hopper lookup ignores the player-facing solid-block obstruction
above either half. Trapped double chests use the same ordering. Ordinary and
trapped single/double chests restore exact viewer count, raw current/previous
lid floats, and the private sync counter; opening/closing also emits the exact
threshold sound with one World RNG draw. Player use exposes a true 54-slot
Large Chest container, routes both halves in canonical order, and renders a
populated panel exactly over every real-Java-owned pixel. All 16 Shulker Box
colors have live
27-slot use/close, animated lids and collision, entity
push, comparator output, ordered audio, teardown, native checkpoints, and
neutral-capsule continuation of viewer/status/raw progress state. Hoppers
accept ordinary insertion/extraction and reject nested shulker items. Their
populated GUI is exact over all 116,792 opaque Java-owned pixels; seven live
TESR states retain three pinned minified edge ties above channel delta 25.
Player use of ordinary and trapped single/double chests separately respects
the exact lower-face side-solid lid check above every joined half. Sitting
ocelot obstruction remains outside the represented entity set.
Represented droppers and dispensers use Java's exact fixed-nine-slot reservoir
selection with an injectible runtime-local 48-bit cursor. Java leaves the
process-global source cursor out of world NBT, so capsule transport is not yet
automatic.
Their nine-slot storage and the hopper's five-slot storage are now exposed as
live player containers. The product uses the real 1.11.2 dispenser and hopper
panels, exact slot coordinates, exact Dispenser/Dropper/Item Hopper titles, and
the same player-inventory and hotbar order as `ContainerDispenser` and
`ContainerHopper`. Pickup, right-half pickup, deposit, merge, quick-move,
throw, distance close, and rich ItemStack persistence route through the shared
live click path. Native checkpoints retain the open kind, active tile, cursor,
and contents. Inventory changes notify adjacent comparators; this also closed
the previously unreachable comparator-override admission for hopper, brewing
stand, and all 16 Shulker Box inventories. These paths run only on player
interaction or inventory mutation and add no idle tick scan.
Populated deterministic real-Java fixtures have zero A/B noise. Native matches
all 116,792 owned dispenser pixels, 116,792 dropper pixels, and 93,560 hopper
pixels exactly, including items, counts, titles, slots, and hover state.
Empty buckets pick up exact water/lava sources, including valid stacked-bucket
routing into the first empty dispenser slot and filled-bucket ejection when the
dispenser is full. Failed pickup delegates to the exact default-item path.
That default path preserves item metadata/damage and now covers ordinary
untagged blocks and items, including non-white dyes and milk buckets, even when
the dispenser faces a solid block. Registered special behaviors remain
explicitly rejected until their own semantics are implemented.
White dye applies exact dispenser bonemeal behavior to wheat, carrots,
potatoes, beetroot, pumpkin/melon stems, cocoa, tall grass, and ferns. Crops
and stems use the exact world-RNG growth draw and clamp at maximum age; cocoa
preserves facing and advances one stage without RNG; grass and fern become the
exact two-block double plant. Mature states and dead bushes preserve the item
and RNG and emit the optional failure events. Sunflowers, lilacs, rose bushes,
and peonies clone an exact item from either half, including constructor pose,
motion, hover phase, same-tick item state, and causal RNG/EID cursors. All six
stage-zero saplings consume Java's exact `nextFloat` draw and promote to stage
one only below 0.45. Stage-one dispenser growth consumes the exact initial
`nextInt(10)` and generates ordinary/big oak, spruce, birch, jungle, and acacia
trees, plus required 2x2 dark oak. Generator failure restores the exact source
sapling state; notified leaf replacement retains Java's write-order-dependent
`CHECK_DECAY` metadata. A dispenser adjacent to a 2x2 spruce or jungle base
correctly blocks those huge generators' clearance and restores all four
saplings. Brown and red mushrooms use the exact 0.4 probability,
height and doubled-height draws, clearance and soil checks, rollback, and
metadata-complete cap/stem generator. Grass bonemeal runs Java's exact
128-attempt random walk, normal-cube and support checks, tall-grass placement,
and Forge-weighted flower selection in every vanilla biome. The exact tables
cover plains and its mutation, swamp and its mutation, flower forest, and the
default-biome dandelion/poppy branch.
The playable main-hand white-dye path raycasts the selected block and shares
that exact plant-growth implementation. It consumes one dye outside creative
mode, emits the 2005 event, and swings on success. Direct player application is
locked for both stage-zero RNG outcomes and all stage-one sapling dispatches,
including successful unobstructed 2x2 spruce, jungle, and dark-oak trees,
single dark-oak failure, big oak, and obstruction rollback. The empty-main
playable route and direct server-call boundary also select offhand white dye,
with the same consumption, event, swing, volume, and RNG results. General
populated-main-hand PASS arbitration before an offhand attempt remains outside
this boundary. Natural random ticks are exact for canonical
supported saplings: Java's light-at-above threshold and `nextInt(7)` branch
feed the same stage/generator body, including every species, 2x2 selection,
failure rollback, raw result volume, and final world-RNG cursor. An isolated
real `WorldServer` rank-zero fixture also matches the selected cell,
`updateLCG`, callback mutation, and world RNG. General loaded-world
section/chunk ordering remains outside this bounded selector proof. Controlled
natural callbacks are also exact on canonical valid support for wheat,
carrots, potatoes, beetroot, pumpkin/melon stems, cocoa, nether wart, cactus,
sugar cane, brown/red mushrooms, grass, mycelium, and farmland. They retain
Java's light gate, fertile-farmland/layout growth bound, beetroot throttle,
cocoa support/age roll, mature-stem adjacent-fruit, four-direction,
target-soil, obstruction, default-state, and RNG ordering.
Unsupported canonical cocoa also follows its exact random-tick teardown: the
pod becomes air and notifies neighbors before its old age state constructs one
or three brown-dye EntityItems, without taking the growth `nextInt(5)` branch.
Nether wart uses the exact `nextInt(10)` immature roll; cactus and cane age
without RNG, reset when they extend, and stop at height three or under a
blocked ceiling. Mushrooms retain the outer `nextInt(25)`, local five-block
density cap, fixed four-step candidate walk, support checks, placement, and
exact final cursor. Grass and mycelium decay under the exact light/opacity gate
or execute their bright four-attempt dirt spread with twelve ordered draws.
Farmland scans the exact 9x2x9 water volume, accepts open-sky rain, hydrates to
seven, dries one moisture level at a time, and retains dry soil under a
supported plant without consuming RNG. Invalid-support drop/item edges outside
cocoa and entity lifting when farmland becomes dirt remain outside this
callback boundary.
Snow layers retain all metadata from one through eight layers when block light
is at most 11 and disappear when block light exceeds 11, without consuming
RNG. Full snow blocks retain through stored block light 11; at 12 they become
air and emit four separate snowball stacks with exact chance/offset draws,
EntityItem Math.random fields, entity IDs, and final RNG cursors. Overworld
ice in the verified flat-stone water basin retains at block
light eight and below; above eight it becomes flowing water and schedules the
exact priority-zero liquid callback five ticks later, also without consuming
RNG. Controlled frosted ice preserves its age-zero-through-three state when
combined light is at most `8-age` and fewer than four face neighbors are also
frosted. Every callback consumes the exact `nextInt(3)` gate and schedules its
priority-zero successor 20 through 40 ticks later. A successful age step
increments metadata; age three follows Java's static-water old-block neighbor
notification order, including sparse-neighbor collapse, then becomes flowing
water with a +5 liquid callback and performs the separate six-face melt pass.
The sparse star and supported dense ring lock both propagation paths, their
queue order, and final RNG cursor. Nether water-vaporization and loaded-chunk
random-tick selection remain outside this callback boundary.
Dry sponges now execute the exact bounded `BlockSponge.absorb` breadth-first
walk from both placement and ordinary neighbor callbacks. The walk preserves
Java's DOWN/UP/NORTH/SOUTH/WEST/EAST order, depth-seven reachable boundary,
post-node over-64 stop, water-removal notification pass, wet metadata, and
2001/data-9 event. A dense 99-water fixture removes 65 cells, wakes 26 retained
boundary cells into flowing water with exact +5 queue rank, and leaves eight
static cells. Wet sponges do not absorb and allow adjacent static water to wake
normally. The callback uses fixed local storage and adds no loaded-world scan.
Nether portal cells now revalidate their complete obsidian frame on ordinary
neighbor callbacks. Minimum X- and Z-axis frames retain all six interior cells
after unrelated edits, while removing a side-frame or interior portal cell
synchronously collapses the remaining interior. The bounded validator follows
the 1.11.2 width/height limits through the legal 21-by-21 maximum and adds no
idle scan, allocation, scheduled work, event, drop, or RNG use.
Portal random ticks now execute the directly compared 1.11.2 pigman spawn
callback. Adult, baby, missed, and disabled-gamerule branches match, as do a
baby mounting an existing chicken and the constructed chicken-jockey branch.
The strict Java/native boundary covers entity/world/UUID RNG streams, entity
IDs, equipment and attack attributes, mount order and pose, five continuation
ticks, and a native checkpoint/reload after tick two.
Four more ordinary neighbor callback classes now follow 1.11.2. Fire without
an opaque floor disappears unless a face neighbor can catch fire. Canonical
cake states disappear without a drop when the material below is not solid.
Floor- and wall-mounted ordinary torches plus north/east ladder attachments
drop their exact registered item before becoming air when support is removed;
valid flammable, cake, and ladder controls retain their original state. The
drop paths preserve exact item motion, visual bits, RNG cursors, and entity ID.
Malformed cake metadata remains rejected atomically, and the callbacks add no
idle scan or allocation.
Standing and wall signs now also follow their exact support-loss callbacks.
Standing signs test the material below; wall signs map horizontal metadata two
through five to the opposite attachment cell. Unsupported signs emit item 323
before becoming air, with exact item state, RNG cursors, and entity ID. Valid
support and unrelated edits retain the sign, and the callback adds no idle scan
or allocation.
Default standing and wall banners now use the same material-solid attachment
rules and drop item 425 before becoming air. Empty flower pots test the exact
fully-opaque support predicate, drop item 390, retire their tile record, and
preserve Java's measured post-drop World RNG cursor. Supported and unrelated-
edit controls retain the original blocks. Occupied pot payload behavior remains
represented by the previously verified tile/drop model; the new direct callback
gate covers the empty pot. No callback adds an idle scan or allocation.
Ordinary block edits also run the bounded 1.11.2 sky-light column update.
Opaque stone, attenuating leaves and water, transparent glass, stacked edits,
overhangs, and local-x15 chunk-edge cases preserve exact saved light, raw block
side effects, delayed water queues, and controlled RNG/entity cursors in the
parked Java/native gate. General unloaded-boundary and deferred gap relighting
remain outside that controlled column boundary.
Ordinary neighbor edits now also run static lava's exact immediate lifecycle.
Water on any face except below forms obsidian from a source or cobblestone from
levels one through four; higher levels and nonmixing states become flowing lava
and schedule the callback 30 ticks later in the Overworld/End or 10 in the
Nether. The nested wakeup of static water, block-category lava-extinguish
sound, exact pitch cursor, and eight
large-smoke spawn coordinates are Java-matched. The playable renderer follows
the 1.11.2 large-smoke constructor and animation formulas with deterministic
constructor-private visual entropy. Bounded dynamic lava sources over
air/stone floors also match Nether/End decay one/two, cadence 10/30, exact
queue order, and the four-step/two-step slope-search split. Bounded non-source
recomputation includes the exact random four-times schedule slowdown and
Forge's default no-source result for two adjacent lava sources. Downward flow
into air creates exact falling level-eight lava and child/source queues in the
Nether and End. Horizontal flow into fire also replaces the block and emits
the exact extinguish sound/smoke stream while preserving child/source queue
and RNG order. Horizontal static/flowing water additionally matches the
source-to-obsidian nested callback, two effect streams, stale water queue, and
child lava; downward water forms stone with exact effects and source requeue.
Exact scheduled callbacks retire only an overlapping approximate live-fluid
region, leaving remote regions active. Valid sapling, web, floor torch, snow
layer, wall vine, and carpet targets are also replaced with exact effects and
queues; ladder remains blocked by Java's explicit exception. The directly
verified replacement family now also includes ordinary rail, an unpowered
powered-rail state, unpowered redstone wire, tall grass, age-three wheat, dead
bush, brown mushroom, and a lit floor redstone torch. Powered rail preserves
its break-hook center-before-child queue order, and dead bush wakes its sand
support onto the exact two-tick falling-block queue. A valid supported reed
remains blocked by Java's explicit liquid exception. Yellow/red flowers, red
mushroom, pumpkin/melon stems, waterlily, nether wart, carrots, potatoes, and
beetroot are directly verified too. Waterlily replacement additionally wakes
its static-water support into flowing water on the exact five-tick queue.
Remaining metadata and other circuit/plant/material families, general dynamic
columns, and unloaded-boundary behavior remain under the broader fluid limits.
Controlled chorus flowers now cover all ages zero through five on direct end
stone and two- or three-deep rooted chorus stems. The callback retains Java's
otherwise-easy-to-miss `nextInt(1)` advance, depth-dependent vertical gate,
random horizontal direction order, obstruction tests, parent-to-plant
replacement, and ordered grow/death events. Stone-above fixtures distinguish
zero-attempt death from one- and two-branch lateral growth; a rooted seed forces
the depth rejection and age-four terminal death. General loaded-chunk
selection, Forge event overrides, and broader mixed-material growth
neighborhoods remain outside this callback boundary.
Vine random ticks now cover the complete vanilla callback over canonical block
states. The implementation retains the one-in-four gate, exact 9x3x9 density
cutoff, six-direction selection, upward and downward face filtering,
clockwise/counterclockwise lateral spread, acquisition of opaque full-cube
faces, lower-vine merging, and Java's distinct attachment predicates. The
locked fixtures include all mutation branches plus no-roll and density
suppression. Neighbor-driven support rechecks are also exact: missing stored
faces are pruned, a matching face can be inherited from the vine above, empty
states break without an item, and support-column loss cascades synchronously.
Loaded-chunk random-tick selection remains a separate boundary.
Leaves now retain the exact four-round, face-connected distance search through
the surrounding 9-cube for both old and new log/leaf families. Unsupported
decay covers every canonical decayable species, exact sapling and oak/dark-oak
apple drop RNG, complete item construction, and flag-three support
notifications. Ordinary log removal marks all leaves in its surrounding
9-cube, ordinary leaf removal marks the surrounding 3-cube, and both include
diagonal and player-placed leaves. Loaded-chunk random-tick selection and
unloaded-boundary behavior remain separate boundaries.
Ordinary flag-three neighbor edits now also use the exact support-loss path for
saplings, tall grass, dead bush, both flower blocks, both mushrooms, wheat,
pumpkin and melon stems, waterlily, nether wart, carrots, potatoes, beetroot,
reeds, cactus, double plants, snow layers, carpets, and cocoa. Unsupported bush
states run their registered drop logic before the air transition. Snow removes
without an item, carpet preserves its color while dropping before removal, and
cocoa validates its horizontal jungle-log support before removing itself and
dropping one or three brown dye stacks. Exact item metadata, positions, motion,
pickup delay, World/Math/Block RNG, entity-ID cursors, and synchronous callback
order are preserved. Two-block reeds/cactus and paired double plants therefore
collapse in Java order instead of being removed by an approximate scan.
General loaded-chunk selection, unloaded boundaries, and support families
outside this represented set remain separate boundaries.
Static source lava now runs its exact random fire callback inside a bounded
proof region containing air, stone, fire, static lava, and any canonical state
whose Java material can burn. Registry-generated `getCanBurn` and
`blocksMovement` masks cover wood, leaves, cloth, TNT, vines, and carpet without
an ID allowlist. The callback preserves the outer `nextInt(3)`, cumulative
upward walk, coordinate draws, first-moving-material abort, three independent
floor probes, and every `BlockFire.onBlockAdded` successor draw and queue rank.
`doFireTick=false` exits before RNG. Unknown non-burning neighborhood states,
unloaded-world boundaries, and general loaded-chunk selection remain outside
this callback boundary.
All six vanilla minecart items dispense onto flat, ascending, or one-block-lower
rails and retain their rideable/chest/furnace/TNT/hopper/command kind. A blocked
rail target follows Java's nested default-ejection path. All 16 untagged
shulker-box colors place with exact support-dependent facing and create their
empty 27-slot tile; solid-target failure preserves the item.
All six boat items retain their wood-type state and exact water/air-over-water
spawn geometry; blocked placement follows the nested default-ejection path.
All 20 armor items plus shields and elytra use exact damage-preserving default
ejection when the target block contains no living entity, and equip the exact
represented-player slot when it is empty. Occupied player slots take Java's
default-ejection and immediate-pickup path. Mob and mixed equipment targets are
rejected explicitly until their equipment state and selection order exist.
Skulls of all six metadata types and pumpkins likewise equip an empty
represented-player head slot or preserve the source on occupied/empty
non-pattern failure. Singleton and stacked equip both match, including Java's
singleton failure-sound edge. Possible wither, snow-golem, and iron-golem
completion is rejected until summon-pattern mutation is represented.

## Product promise

`make -C magma game` builds one native Minecraft 1.11.2 simulation binary. In a
default world, a human or scripted survival player can start with an empty inventory,
legally reach the End, kill the dragon, enter the exit portal, and receive a terminal
`won` observation. The world is in memory and an episode ends on death or victory.

The simulator is behaviorally faithful for supported mechanics, deterministic between
its CPU and CUDA implementations, and objectively checked against Java Minecraft.
Simulation need not reproduce Java RNG call order or every floating-point bit. Rendering
is compared numerically against pinned Java scenes; it is never approved by a subjective
"looks right" play session.

## Fixed product choices

- Survival is the only game mode. Creative, spectator, adventure, hardcore, commands,
  cheats, bonus chests, and arbitrary gamerules do not exist in the product interface.
- Single-player only. There is no networking, LAN, server split, or multiplayer state.
- No player-facing disk saves or general NBT persistence. Deterministic reset,
  in-memory snapshots, and the bounded verification/import capsule may exist
  as harness operations. The capsule now crosses a real Java fresh-object NBT
  reload for all 34 plain native living types and continues them for 20 exact
  ticks. The represented common state includes bounded flagged potion effects,
  effect-derived health and absorption, and authoritative entity AABBs; that
  does not make the neutral importer permissive for arbitrary unsupported NBT.
- Redstone power, automation, rails, and minecarts are full-parity expansion
  systems. Their implemented subsets are available behind their live paths and
  their remaining coverage is tracked in `PARITY_PROJECT.md`; the fast base
  profile does not pay for inactive systems. Interactive play now has optional
  OpenAL playback for the represented ordered event stream, including bounded
  streaming for all 12 jukebox records and exact distance-delayed firework
  blast/twinkle playback plus material-exact player/grazing block-break and
  successful ItemBlock placement sounds. Progressive mining emits the exact
  material hit family and Java cadence, category, position, volume, and pitch.
  Damage landings emit the ordered player small/big-fall sound and supporting
  block material sound, including hay's reduced damage threshold.
  Walking emits distance-gated material footsteps with the exact snow-layer,
  ground-sneak, and riding rules.
  Note blocks retain pitch and prior-power tile state, play only on a rising
  redstone edge, tune modulo 25 on right-click, and play without tuning on
  left-click. The material-selected harp, bass drum, snare, hat, and bass
  sounds and NOTE particle descriptor match Java exactly. Their owned sound
  assets and bounded six-tick NOTE particle renderer are wired into live play.
  Water entry and swimming emit exact player splash/swim events, including
  motion-scaled volume and the particle-coupled client RNG cursor.
  Player melee emits exact knockback/sweep/critical/strong/weak/no-damage
  events, including accepted/rejected order and movement-gated sword sweeps.
  Its four represented combat-particle classes preserve exact server event
  descriptors and bounded vanilla lifecycle/render formulas; constructor-only
  client entropy and resulting individual pixel placement remain a residual.
  Water entry also renders the ordered bubble/splash burst through the bounded
  client particle pool; its Java wall-clock constructor entropy is not exact.
  Lingering clouds render their exact Java particle-call stream through the
  same bounded pool, including active-disc and waiting-cloud cadence and tint;
  their separate unsaved ParticleSpell constructor entropy is deterministic.
  Music, ambient loops, category controls, broad achievements and statistics,
  scoreboards,
  resource packs, skins, and a broad graphics-options menu remain absent.
- Difficulty is fixed to Normal. Day/night, mob spawning, drops, fire required for
  portals, random ticks required by the route, and the mandatory structures stay on.
- Death terminates the RL episode. Reset creates a fresh deterministic world.

## Required completion route

The acceptance route is narrower than "every possible 1.11.2 speedrun", but it keeps
the common fast-route mechanics:

1. Spawn, find and break a tree, receive item entities, and pick them up.
2. Craft planks, sticks, a crafting table, wood/stone tools, a furnace, and iron gear.
3. Obtain food, wool and a bed, gravel and flint, buckets, water and lava; form and
   ignite a Nether portal.
4. Traverse a correctly linked portal into generated Nether terrain.
5. Locate a generated fortress, fight blazes, collect rods, and craft blaze powder.
6. Fight naturally spawned endermen, collect pearls, craft and throw eyes of ender.
7. Locate a generated stronghold, activate its portal frames, and enter the End.
8. Fight the dragon and crystals using melee, projectiles, and bed explosions. Beds must
   explode in the Nether and End because that is a standard speedrun mechanic.
9. Complete the dragon death sequence, create the exit portal, enter it, and set `won`.

This requires faithful movement/collision, swimming, ladders/vines, reach/raycast,
breaking/placing, stack and inventory rules, 2x2/3x3 crafting, furnaces and fuels,
chests/loot, tools and durability, food and vitals, buckets and fluid reactions, fire,
portals, combat/damage/death, projectiles, explosions, item entities, entity spawning,
dimension ticks, lighting, and route-relevant block ticks.

Mandatory world generation is default Overworld terrain/biomes, caves, ravines, trees,
ores, lakes/lava pools, Nether terrain and fortress, stronghold and portal room, and the
central End island, towers, crystals, dragon arena, podium, and exit portal. Strongholds
and fortresses are baseline systems and are never hidden behind a global structures flag.

The required encounter roster is the common speedrun-visible subset: sheep, pigs, cows,
chickens, zombies, skeletons, creepers, spiders, slimes, endermen, blazes, zombie pigmen,
ghasts, magma cubes, wither skeletons, silverfish, the dragon, end crystals, items,
arrows, boats, and XP entities. Rare-biome and side-content mobs remain out of scope until
a route demonstrates a need for them.

## Runtime feature bundles

These are coherent launch-time bundles and default to `off` for RL throughput. Enabling
a bundle must either provide the entire documented behavior or fail at startup. A flag
must never silently do nothing.

- `villages`: vanilla-faithful village generation, farms, blacksmith/chest loot,
  villagers, golems, and trading. Disabling it must not disable strongholds or fortresses.
- `enchanting`: XP/lapis costs, tables, bookshelves, enchant application/effects, and
  the required inventory UI and render glint.
- `brewing`: Nether wart, five-slot stands/fuel, all 1.11.2 recipes, bottle
  filling, drinkable potion effects and milk cure, persistence, comparator
  state, UI, exact seeded splash/lingering launch, player impact scaling,
  instant player/mob effects, water-fire extinguishing, water damage to
  blazes/endermen, and per-target lingering-cloud timing. Test-only cold
  fixtures and Java-authored state capsules can resume an in-flight default
  potion or the complete represented scalar lifecycle of a default lingering
  cloud at a pre-tick boundary. Represented mobs
  retain exact bounded effect duration/amplifier state with vanilla combine
  rules; regeneration, poison, undead applicability, same-tick fire
  resistance, Speed/Slowness movement, Strength/Weakness melee, and Jump Boost
  are live. Resistance reduces represented incoming damage after hurt
  immunity and Wither uses exact cadence plus ordinary death/drop. Health
  Boost, Absorption, and Levitation are also live, including effect replacement
  and removal side effects. Water Breathing holds the represented mob air
  counter underwater; dry reset, drown cadence, bubble RNG, damage, and raw
  save-state exposure are live. Invisibility drives the live mob render flag,
  suppresses base and slime-gel geometry, expires before the next rendered
  state, and does not suppress the separate fire overlay. Custom drinkable,
  splash, and lingering potions share one ordered base-plus-
  custom effect path. The bounded payload retains amplifier, duration, ambient
  and particle flags, custom color, and the thrown stack's arbitrary NBT; the
  drink path returns the exact glass bottle. Active Saturation updates player
  food/saturation, while the vanilla no-op instant potion path is preserved.
  Luck and Unluck combine through the player attribute and feed fishing loot
  plus player-aware single-chest deferred fill. All 80 other built-in loot
  tables are source-censused as output-invariant because their quality and
  bonus-roll fields are zero; a synthetic real-Java gate locks both Luck
  formulas and the complete shuffled inventory result.
  Nausea now drives the exact client float ramp, duration-60 cutoff, explicit
  removal reset, portal-overlay suppression, seven-degree camera phase, and
  checkpoint continuation. Glowing state and the entity-outline post-process
  are live and oracle-gated. Arbitrary custom loot-table loading is owned by
  the general loot task. Strict Nausea/portal pixels, common cloud entity
  NBT beyond promoted identity/kinematics, statistics/criteria, and
  cloud-particle pixel
  promotion remain under active parity work.
  Blaze rods and blaze powder remain mandatory even when brewing is off.
- `weather`: rain/thunder timers, mechanics, lightning, wet/fire interactions, and
  rendering. Off means permanently clear with no weather tick or render cost; day/night
  still advances.

`superflat` is a supported RL arena using only the vanilla default flat layers. It is not
required to be naturally completion-capable and arbitrary custom flat presets are cut.

## Product launch interface

The intended narrow interface is:

```text
magma_game
  --seed <i64>
  --world default|superflat
  --villages on|off
  --enchanting on|off
  --brewing on|off
  --weather on|off
  --render off|window
  --backend cpu|cuda
  --pace realtime|unlimited
  --view-distance <supported integer>
  --width <pixels> --height <pixels>
```

Defaults are seed 0, default world, every optional bundle off, window rendering, CPU
reference backend, realtime pacing, view distance 8, and 854x480. RL runs explicitly
select rendering off and unlimited pacing. Unsupported values, duplicate settings, and
settings not wired in the current build exit with status 2 and a precise error.

The binary also owns the test/RL control path:

```text
magma_game --headless --ticks N --script events.jsonl \
  --state-out state.jsonl [--frames-out frames/]
```

Scripts apply typed events at a documented pre-tick boundary: survival actions plus test
injections for pose, velocity, block state, inventory, time/weather, and entities. These
are harness mutations, not a creative game mode. Interactive and headless execution must
share one tick function; a second drifting simulation loop is forbidden.

`--frames-out` writes one deterministic `frame_NNNNNN.ppm` after each executed tick.
It includes the same terrain, entity, first-person hand, and HUD passes, follows world
time, and is bit-identical between the CPU and CUDA binaries for the tested scenes.

## Test-hook boundary

Component tests may inject any state. The macro completion test may inject pose, velocity,
time, and travel shortcuts between separately verified generated landmarks. It may not
inject route-critical inventory, portal blocks or dimension transitions, mob drops or
deaths, dragon health/death, the exit portal, or `won`. Acquisition, crafting, portal
activation, combat, dragon death, and victory must occur through survival actions.

## Visual acceptance

- Compare C and Java from the same serialized scene state and render tick. Independently
  ticking both games before a pixel diff is invalid because simulation timing may differ.
- Pin one canonical profile instead of exposing Minecraft's graphics matrix. The profile
  fixes FOV, gamma, Fast graphics, smooth lighting, mipmaps, particles, clouds, shadows,
  GUI scale, and animation phase. Weather gets separate off/on scenes when it ships.
- Pixel gates include HUD, held items, inventory screens, mobs, portals, fluids, particles,
  dragon phases, and the death sequence. They may not mask the difficult regions.
- Calibrate numeric tolerance from repeated Java-versus-Java captures of pinned state,
  commit the measured noise threshold, and fail automatically above it. Do not guess a
  threshold and do not require the user to visually inspect the whole game.

## Explicit side-content cuts

Mineshafts, dungeons, and igloos are promoted. The represented breeding path, fishing
gameplay core, fireworks/elytra gameplay, outer-End population, gateways,
cities/ships, village generation, and temples are also live under their
parity-project limits. Ordinary village residents and their initial trade slice
are live, while villager AI, later economy state, and golems remain cut. Hut
witch materialization/base state, bounded self-potion lifecycle, and ranged
splash-potion combat are live. Witch lifecycle audio and player-credited loot
are also live. Player-credited terminal XP covers both ordinary Witches and a
represented drinking Witch, including its optional held-potion equipment drop,
equipment XP bonus, split orbs, and terminal particle ordering. Held/status
rendering, other unpromoted non-player lethal paths, general-mob suffocation,
landing damage/effects beyond the bounded ordinary-Witch cases, arbitrary
equipment/NBT persistence, and pixel promotion
remain cut. Drowning-, falling-anvil-, periodic-ON_FIRE-, lava-,
block-fire-contact-, cactus-, wet-block-fire-, rain-exposed-block-fire-,
in-wall-, and represented small/big-stone or hay-fall-killed
ordinary Witches are promoted through exact
Looting-0 table loot, source-specific feedback,
no-equipment/no-XP credit rules, terminal
particles, fixed-capacity atomicity, and exact base ambient RNG/audio ordering.
Nonlethal ordinary Witch landings additionally cover Jump Boost II damage
avoidance, non-sneaking living slime bounce, 15/16 farmland collision,
seeded trample, and `mobGriefing` suppression with exact entity/world RNG.
The same mechanical slime bounce and low-speed walk damping now run through
the shared ordinary-living product path; real-Java raw-state rows cover
zombie, sheep, and Witch, product ticks cover hostile/passive families, and
all represented living types emit the exact generic BLOCK_DUST descriptor.
Ordinary Witch flowing-water acceleration and dry-to-water splash sound/RNG
and rendered bubble/splash payloads are also promoted; pixel promotion of the
constructor-private particle details remains cut.
Ocean monuments now have exact spacing/biome candidates, room graph, complete
four-facing clipped placement, treasure/sponge variants, and three persistent
elder sites. Guardian and Elder Guardian attributes, laser/thorns/loot/audio,
model animation, beam, and mining-fatigue delivery are live. Ordinary Guardian
natural-spawn pack ordering, monument locate/save/load and arbitrary chunk
ordering, and monument/Guardian pixels remain open. Villager task AI, broader
decorative behavior, and the documented residual edges of promoted bundles
also remain open full-parity work. Wolves and ocelots, shulkers, dragon
resummoning, and the interactive merchant screen are promoted under their
focused Java/native gates. Woodland mansions now have exact graph/template
placement, loot, and persistent Vindicator/Evoker residents. Exact direct
Evoker attack, summon, and Wololo boundaries, fang damage/lifecycle, bounded
Vex flight/charge/lifespan behavior, aggressive/casting poses, held weapons,
textures, and audio are live. General task/NBT continuation and pixels remain
open. Static scenery emitted by a supported generator
still needs correct model, texture, collision, and light behavior.
