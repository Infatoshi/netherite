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
flat 16x16 item icons; `assets/build_gui_atlas.py`), and the crafting-table and
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

The JSONL runner also exposes strictly typed test-only pose, velocity, time, weather,
block, inventory, and entity mutations. They execute before the shared tick and are not
available through normal survival input.

Remaining product gaps are not hidden by those gates: the inventory 3D
player-model preview is rendered and ROI-gated but is not claimed pixel-perfect
until bit-exact (see `run_gui_verify.sh` residual + calibration report),
block-items draw as flat texture tiles where vanilla renders mini 3D blocks, and
item ids outside the atlas table fall back to colored pips; world chest blocks
still use a static inset mesh without the animated lid/TESR texture, and several
exact type-specific entity animations/particles remain simplified;
the `--villages on` development path now enables exact recursive village
pieces, roads, farms/crops, houses/doors, blacksmith loot, and retained
resident spawn sites/professions. Ordinary residents now materialize into the
live mob store, render profession-specific nine-part models, and expose 22
Java-locked initial ordinary offers across 11 tested career selections. It is
not yet a complete product bundle: villager AI, reputation/door state,
breeding, golems, later trade tiers/restocking, enchanted/map offers, merchant
UI, persistence, and pixel promotion remain open. Enchanting `on` now
enables exact bookshelf scanning, offer generation/reseeding, item/lapis slots,
XP/lapis costs, application, and book conversion in a playable table UI, while
anvils, complete enchanted-book paths, glint, and pixel promotion remain open.
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
rollback, and cooked lethal loot are exact. Its remaining sound, particle,
sweep, Thorns,
player-target velocity acknowledgement, and statistics side effects are still
open. The remaining mob effect behaviors, cloud particles, and
potion-entity persistence remain open; and
the Java-pixel suite does not yet cover every required HUD/entity/particle state. The
instrumented seed-0 and seed-7 terrain gates and the Java Nether/End portal suite pass.

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
- No disk saves or NBT persistence. Deterministic reset and in-memory test snapshots may
  exist as harness operations, not as player-facing saves.
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
  Music, ambient loops, category controls,
  achievements, statistics, scoreboards,
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
  fixtures can resume an in-flight potion or the complete represented scalar
  lifecycle of a lingering cloud at a pre-tick boundary. Represented mobs
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
  state, and does not suppress the separate fire overlay. Remaining
  mob world/render and non-brewing effect behaviors, cloud
  particles, automatic capsule translation, and full potion-entity NBT
  persistence remain under active parity work.
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

Mineshafts and dungeons are promoted. The represented breeding path, fishing
gameplay core, fireworks/elytra gameplay, outer-End population, gateways,
cities/ships, village generation, and temples are also live under their
parity-project limits. Ordinary village residents and their initial trade slice
are live, while villager AI, later economy state, and golems remain cut.
Monuments, woodland mansions, taming/pets, maps, shulkers, dragon resummoning,
broader decorative behavior, and the documented residual edges of those
promoted bundles also remain cut. Static scenery emitted by a supported
generator still needs correct model, texture, collision, and light behavior.
