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
player-model preview is a gated ROI under `pin_preview_anim`
(ageInTicks=0, pose1 parked mouse + held-out pose2 on inv slot A): PASS if
bit-exact; PASS-LSB if A/B noise is 0, every differing pixel is at most 1 LSB
in every channel, and nz <= 2% of the ROI; otherwise FAIL. PASS-LSB is a
guarded rounding tier, not a circular PASS-FLOOR. A mutation self-test must
still fail uniform +1, a single +2 pixel, and a 3x3 +12 recolor.
`gui_preview_calibration.json` records the tier verdict and guard results.
The ui_hud oracle ROI gate (`verify/ui_hud/run_ui_hud_gates.sh`) uses the
same three verdicts on hand viewmodels and fullscreen exact-bar overlays:
PASS if bit-exact; PASS-LSB if A/B noise is 0, every differing owned pixel
is at most 1 LSB, and nz <= 2% of the owned ROI; otherwise RESIDUAL/FAIL.
A mutation self-test (`verify/ui_hud/ui_hud_lsb.py`) must still fail
uniform +1, a single +2 pixel, and a 3x3 +12 recolor, and pins the live
eat/shield residual as RESIDUAL. Core HUD ownership checks are unchanged.

The JSONL runner also exposes strictly typed test-only pose, velocity, time, weather,
block, inventory, and entity mutations. They execute before the shared tick and are not
available through normal survival input.

Remaining product gaps are not hidden by those gates: the inventory 3D
player-model preview is rendered and ROI-gated (PASS or PASS-LSB; not claimed
pixel-perfect while nz>0; see `run_gui_verify.sh` residual + calibration),
block-items draw as flat texture tiles where vanilla renders mini 3D blocks, and
item ids outside the atlas table fall back to colored pips; world chest blocks
still use a static inset mesh without the animated lid/TESR texture, and several
exact type-specific entity animations/particles remain simplified;
optional villages/enchanting/brewing/weather bundles deliberately reject `on`; and
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
- No redstone power or automation, rails, minecarts, audio, achievements, statistics,
  scoreboards, resource packs, skins, or broad graphics-options menu.
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
- `brewing`: Nether wart, stands/fuel, route-relevant recipes, potion effects, and UI.
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

Mineshafts, dungeons, temples, monuments, woodland mansions, End cities, pets, breeding,
fishing, maps, fireworks, decorative block behavior, and outer-End gateway/resummoning are
cut until promoted by an RL task or supported speedrun route. Static scenery emitted by a
supported generator still needs correct model, texture, collision, and light behavior.
