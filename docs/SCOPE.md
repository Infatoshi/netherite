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

## 1. Cut (not in scope)

- Redstone mechanics (wire, repeaters, comparators, pistons, dispensers,
  rails, TNT-as-circuit). Only redstone-ore blocks and a wire selection box
  exist. (`docs/DEVLOG.md:54`)
- Multiplayer/servers, audio, disk saves as a product feature.
- Villages/villagers/trading, enchanting, brewing, weather: `--villages on`
  etc. hard-reject at startup as "not wired yet" (`magma/game/config.c`).
- Side structures (monuments, mansions, temples, igloos) and rare-biome mobs
  (wither, guardians, shulkers, wolves, ocelots...) until a route needs them;
  the roster is the speedrun-visible subset (`magma/PRODUCT.md`).
- Minecarts/rails as simulation (render models only), maps, anvils, beacons,
  jukeboxes, note blocks, signs, doors, pressure plates, hoppers.
- Horses/donkeys/mules (entity skipped), zombie villager as a distinct model.
- Versions other than 1.11.2.

## 2. Open (in scope, tracked in OPEN_DIVERGENCES/VERIFY)

Simulation:
- General random block ticks are wheat-only: no fire spread, leaf decay,
  grass spread, other crops (`VERIFY.md:273`).
- Nether chunk POPULATION (fire blocks, lava springs, glowstone, quartz,
  magma blocks) is not seed-derivable (`ChunkProviderHell.populate` consumes
  leftover RNG, chunk-load-order dependent); replay carries it via world
  snapshots instead. This is why lava pools on Nether/elytra tapes are only
  as complete as the snapshot patch (`OPEN_DIVERGENCES.md` "Nether arrival").
- Entity-driven world edits (crystal-explosion fire) do not replay.
- Eating/drinking/shield use poses; absorption hearts; heart-flash blink.

Rendering:
- Animated texture PHASE (fire/lava/water/portal) has no oracle pixel gate;
  the oracle pins animations (see 3) and the nether tape's animation-phase
  item is open. Assets for all 32 frames exist.
- Particles are a reconstruction, not a ParticleManager port; dig particles,
  enderman teleport particles, dragon per-texel death dissolve.
- Block-items in GUI draw as flat 16x16 tiles, not mini 3D blocks (honest
  stand-in, `assets/build_gui_atlas.py`). Unmapped item ids fall back to pips.
- Enchantment glint, fire overlay composition, rain rendering, distance haze
  strength, portal warp (implemented, unwired), underwater overlay
  (CAPTURE_BLOCKED), inventory 3D player preview (close, not bit-exact),
  slime gel translucency, chest lid animation.

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
