# SCOPE - what magma does not have (yet), and what is deliberately pinned

One page answering "is X in the game?" honestly. Four different kinds of "no"
live here; do not conflate them:

1. **Cut** - not in scope, no open work, saying otherwise would be a lie.
2. **Open** - in scope, partially built or known-wrong; tracked in
   `magma/OPEN_DIVERGENCES.md` (the resolve queue) and `magma/VERIFY.md`
   (the coverage ledger). This file only summarizes; those stay authoritative.
3. **Pinned** - vanilla behavior deliberately suppressed in the ORACLE (via
   `java/fast.yaml` flags and mixins) so pixels are deterministic; magma
   mirrors the pinned look. These are not magma bugs and not secret edits -
   each has a flag, a mixin with a javadoc, and an unpin path.
4. **Unrecoverable from tape** - the recorder cannot capture it even in
   principle; fixing requires a recorder change, not a C change.

For the exhaustive arbitrary-save completion queue, including every Java
entity/tile registry row and the save-fork differential plan, see
`docs/COMPLETENESS.md`.

## 1. Cut (not in scope)

The full-parity project promotes the complete vanilla single-player 1.11.2
surface into scope. Features that were cuts in the original speedrun/RL
product are now open work and are tracked in `magma/PARITY_PROJECT.md`.

- Multiplayer protocol/server compatibility and Realms.
- Forge/mod behavior beyond the explicitly represented vanilla/Forge hooks.
- Account, launcher, and skin-service replication.
- Versions other than 1.11.2.

## 2. Open (in scope, tracked in OPEN_DIVERGENCES/VERIFY)

Simulation:
- Loaded-chunk random-tick membership/order and several remaining block
  callback families are not globally exact. The promoted crop, stem, wart,
  sapling, grass/mycelium, mushroom, cactus, sugar-cane, fire, frost, sponge,
  portal, sign/banner/pot, cake, torch, ladder, and chorus paths are exact in
  their bounded Java fixtures.
- Saved village collection clocks, ordered doors, reputation, and bounded
  ticking now restore exactly through the neutral capsule. The promoted active
  villager slice covers a fresh-NBT 20-tick continuation plus bounded door,
  mating/birth, willingness, farming/sharing, follow-golem, play, rose, golem,
  and siege paths under direct Java/native gates. Arbitrary path/target task
  stacks, door discovery across general terrain, reputation/aggressor effects,
  cross-class loaded ordering, ordinary Guardian natural-spawn ordering, the
  remaining passive/utility mob roster, and general entity/task NBT
  continuation remain the final arbitrary-world promotion program. Ocean
  monument candidates, biome
  viability, room graph, complete clipped block placement, and three persistent
  elders are promoted. Guardian/Elder attributes, laser/thorns/loot/audio,
  model animation, beam, and mining-fatigue delivery are live; monument
  locate/save/load, arbitrary chunk ordering, natural spawn packs, and pixels
  remain open. Woodland mansion generation, templates, chests, and
  resident illagers are promoted. Evoker attack, summon, and Wololo spells,
  fang lifecycle, bounded Vex AI/lifespan, spell poses, textures, and audio are
  live under a direct Java/native oracle; arbitrary continuation and pixels
  remain open.
- Rail curves, general cart riding/collisions and remaining redstone/piston
  topologies remain open beyond the promoted exact straight/slope and
  automation paths.
- Nether chunk POPULATION (fire blocks, lava springs, glowstone, quartz,
  magma blocks) is not seed-derivable (`ChunkProviderHell.populate` consumes
  leftover RNG, chunk-load-order dependent); replay carries it via world
  snapshots instead. This is why lava pools on Nether/elytra tapes are only
  as complete as the snapshot patch (`OPEN_DIVERGENCES.md` "Nether arrival").
- Entity-driven world edits (crystal-explosion fire) do not replay.
- Eating/drinking/shield use poses. The underlying item consumption and shield
  gameplay paths are represented; their first-person transforms remain strict
  pixel residuals.

Rendering:
- Animated texture PHASE (fire/lava/water/portal) has a focused mutation gate,
  but arbitrary live-scene phase/composition remains open. The oracle pins
  animations in canonical recordings (see 3). Assets for all 32 frames exist.
- Particles are a reconstruction, not a ParticleManager port; dig particles,
  enderman teleport particles, dragon per-texel death dissolve.
- Block-items in GUI draw as flat 16x16 tiles, not mini 3D blocks (honest
  stand-in, `assets/build_gui_atlas.py`). Unmapped item ids fall back to pips.
- Enchantment glint, fire overlay composition, rain rendering, distance haze
  strength, portal warp (implemented, unwired), portal and underwater
  live-scene composition, inventory 3D player preview (max-channel-1 shading
  residual plus sparse held-out edge pixels), slime gel translucency, and chest
  lid animation. The isolated core HUD states, including absorption and hurt
  flash, and both inside-block overlays are bit-exact against stable Java
  pairs; the three hand-use poses, portal, underwater, and all 16 focused
  entity/particle families remain strict nonzero pixel gates.

Promoted gameplay that this page previously listed as cut now includes broad
redstone and pistons, automation, straight/slope minecarts, villages and
trading with an interactive merchant screen, enchanting/anvils, brewing,
weather/lightning, audio playback, temples/igloos/huts, outer End/end cities,
dragon resummoning, shulkers, wolves/ocelots, fishing, breeding, and fireworks.
See the ordered table in `magma/PARITY_PROJECT.md` for their remaining edges.

## 3. Pinned in the oracle (deliberate, flagged, mirrored by magma)

Source of truth: diff `java/vanilla.yaml` (human play) vs `java/fast.yaml`
(tape recording), plus `java/Minecraft/.../Malmo/Mixins/*.java` javadocs.

- `determinism.pin_texture_animations: true` - fire/lava/water/portal atlas
  sprites frozen on frame 0 (`MixinPinTextureAnimations`). Vanilla fire DOES
  flicker; we pin it in the recording profile because the animation clock is
  the client's render clock and magma's atlas is frame-zero.
  `run_anim_verify.sh` is the unpinned path. Un-pinning for good means
  implementing the .mcmeta frame cycle in magma keyed to the tick - open.
- `determinism.pin_flicker: true` - torch-light flicker is `Math.random()`,
  the one unseedable RNG feeding pixels (`MixinPinTorchFlicker`).
- `determinism.pin_skin: true` - Steve arm always (Malmo randomizes per
  launch).
- `strip.menus/overlays/sound: true` - pause/death screens, boss bar, sound
  engine suppressed in recordings. magma nonetheless implements the death
  screen and boss bar; they gate separately.
- Video profile pinned identically in BOTH yamls (fov 70, rd 8, clouds off,
  ao 0, mipmap 0, fancy off, shadows off, bob off, gamma 0, particles
  minimal). magma mirrors exactly this look; do not turn fancy back on
  without re-goldening.
- World clock frozen at 6000, weather clear, daylight/weather cycles off.
- NOT pinned, on purpose: `NoAI`, `naturalRegeneration`, `doMobSpawning`,
  `doFireTick`, `doMobLoot`, `mobGriefing`, `randomTickSpeed` all stay at
  vanilla values in fast.yaml - "do not strip gameplay from the oracle".

## 4. Unrecoverable from tape (recorder limits, not C bugs)

- Particle placement RNG (`Entity.rand`/`Particle.rand`, seeded from system
  time): extent/brightness/decay match, puff-for-puff placement cannot.
  Fix path: record `spawnParticle` calls.
- Client vs server clock skew (geared dragon tape: 6 ticks); tape records
  only the client clock. Fix path: log server `processDragonDeath` tick.
- `EntityRenderer.fogColor1` smoother history before record start (every
  tape 2-6x worse at t=0).
- Nether populate RNG (above), dimensions first entered mid-recording on
  legacy tapes, evolved-save world state (hence the fresh-world rule).
- Legacy tape schema holes: EntityItem render state, arrow ghost pitch,
  full GUI interaction record.

Maintenance rule: when an item here is closed or a new cut/pin/blocker is
decided, update this file in the same commit that changes
OPEN_DIVERGENCES.md/VERIFY.md. Cross-references by section title, not line
number.
