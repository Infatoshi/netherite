# Minecraft Java 1.11.2 deterministic edge-case sweep

## Scope and evidence

This list is ranked by demo visibility, determinism in the tape harness, and likelihood that magma differs. It is deliberately specific to Java 1.11.2. Exact constants were checked against the repo's pinned Forge 1.11.2 source archive, chiefly `Entity`, `EntityLivingBase`, `EntityPlayer`, `FoodStats`, `EntityBoat`, `EntityMinecart`, `EntityTNTPrimed`, `EntityFallingBlock`, and `EntityXPOrb`. Linked upstream sources establish release history or the shipped bug. Current wiki prose is used only where it agrees with that source.

Scenario snippets are sketches, not copy-paste-complete fixtures. They use the vocabulary of `verify/scenarios/*.yaml`. Coordinates assume the usual flat-world ground near Y=3. All input times are seconds at 20 ticks/s. For fluid fixtures, put commands in one `setup_qrl: runcmds` step and give it `settle_ticks` when a settled flow field is intended.

Useful common prelude:

```yaml
world: {seed: 1112, mode: survival, type: flat, structures: false}
setup_commands:
  - /gamerule doDaylightCycle false
  - /gamerule doWeatherCycle false
  - /gamerule doMobSpawning false
  - /time set day
input: {segments: [{seconds: 2.0, keys: [], buttons: []}]}
duration_ticks: 80
```

`physics` below means the tape/player or entity state: position, velocity, collision/on-ground flags, fall distance, health, air, food/saturation/exhaustion, fire timer, inventory/durability, and entity lifetime where exposed. `pixels` means the recorded frame comparison. `both` means neither state nor rendering alone is a sufficient oracle.

## Ranked candidates

### 1. Drowning counter, damage cadence, bubbles, and underwater camera

**Behavior.** A player starts with `Air=300`. While the eyes are in water, air decrements once per tick. When it reaches `-20`, it resets to `0`, deals 2.0 HP of drowning damage, and the Java client emits eight bubble particles. Leaving the eye-water test resets air to 300. Damage remains subject to the normal hurt-resistance machinery. The air-bubble HUD and underwater fog/overlay are separate pixel contracts.

**1.11.2 caveat.** This predates the 1.13 swimming and bubble-column rewrite. There is no swimming pose. Netherite already lists underwater fog and the suffocation-style camera overlay as open visual risks, so this is the best launch demo.

**Sources.** [EntityLivingBase NBT air description](https://minecraft.wiki/w/Template:Nbt_inherit/entity/template), [water mechanics and drowning overview](https://minecraft.wiki/w/Minecraft_Wiki:Projects/Esperanto_translation/Akvo), pinned 1.11.2 `EntityLivingBase#onEntityUpdate`.

```yaml
setup_commands:
  - /fill -2 3 -2 2 8 2 glass 0 hollow
  - /fill -1 4 -1 1 8 1 water
  - /tp @p 0.5 4.0 0.5 0 0
input: {segments: [{seconds: 20.0, keys: [], buttons: []}]}
duration_ticks: 400
```

**Assert:** both. Check air at tick boundaries, first drowning event after the `300 -> ... -> -20` sequence, 2 HP damage, bubble HUD depletion, eye-height transition, underwater fog/overlay, and resurfacing reset.

### 2. Flowing-water force and automatic liquid-edge hop

**Behavior.** Each overlapping water cell contributes its flow vector. The normalized sum adds `0.014` blocks/tick to entity velocity. Normal water travel uses `moveRelative(..., 0.02)`, then X/Z and Y damping of `0.8`, followed by `motionY -= 0.02`. When horizontally colliding at a liquid edge, Java tests an offset AABB and sets `motionY = 0.30000001192092896` if it is clear.

**1.11.2 caveat.** The edge hop is the behavior reported as MC-44560 and is plainly present in 1.11-era source. Later water physics and swimming behavior must not be imported. Magma has a bit-matched edge-hop tape, but flow combinations and pixels remain high-value regression material.

**Sources.** [MC-44560](https://bugs-legacy.mojang.com/browse/MC-44560), [fluid flow overview](https://minecraft.wiki/w/Fluid), pinned 1.11.2 `BlockLiquid#getFlow`, `Entity#handleWaterMovement`, and `EntityLivingBase#travel`.

```yaml
setup_qrl:
  - cmd: runcmds
    action:
      commands: ["/fill -4 3 -2 4 5 2 stone 0 hollow", "/setblock -3 5 0 water", "/setblock 3 4 0 stone", "/tp @p 1.2 4.0 0.5 90 0"]
    settle_ticks: 100
input: {segments: [{seconds: 4.0, keys: [w], buttons: []}]}
duration_ticks: 100
```

**Assert:** both. Compare per-tick velocity before input, diagonal vector normalization, contact flags, the exact `0.30000001192092896` hop, and water surface/flow geometry.

### 3. Lava damage, fire timer, and magma-block invulnerability interaction

**Behavior.** While in lava, Java attempts 4.0 HP lava damage every tick and calls `setFire(15)`, meaning 300 fire ticks, without shortening a longer existing timer. Ordinary fire tries 1.0 HP whenever the entity fire counter is divisible by 20. In lava, fall distance is halved each tick. Normal hurt resistance means these attempted sources do not simply add every tick. A magma block attempts 1.0 HP `HOT_FLOOR` damage on contact unless the entity is fire-immune or wears Frost Walker.

**1.11.2 caveat.** MC-102158 is a shipped, confirmed-as-designed 1.11.2 quirk: frequent magma-block damage attempts can consume invulnerability windows and thereby cancel or delay fire damage. This exact stacking behavior is more important than the nominal damage values.

**Sources.** [MC-102158](https://bugs-legacy.mojang.com/browse/MC-102158), pinned 1.11.2 `Entity#setOnFireFromLava`, `Entity#onEntityUpdate`, and `BlockMagma#onEntityWalk`.

```yaml
setup_commands:
  - /fill -2 3 -2 2 3 2 magma
  - /setblock 0 4 0 lava
  - /tp @p 0.5 4.0 0.5 0 0
input: {segments: [{seconds: 12.0, keys: [], buttons: []}]}
duration_ticks: 240
```

**Assert:** both. Record attempted versus effective damage cadence if possible, health, hurt timer, fire timer, fall distance, fire overlay, lava surface, and death timing.

### 4. Elytra flight continues through water and lava

**Behavior.** In 1.11.2, an already-gliding player can enter water or lava without the elytra flag being cleared. The player stays in the horizontal elytra pose, and 1.11.1+ firework boosting remains usable in liquid. Solid ground, not liquid contact, ends the state.

**1.11.2 caveat.** This is MC-97190, reported against and reproducible in 1.11.2. It was classified as working as intended at the time. Do not substitute modern swimming-pose logic.

**Sources.** [MC-97190](https://bugs-legacy.mojang.com/browse/MC-97190), [official 1.11.2 release notes](https://www.minecraft.net/en-us/article/minecraft-1112-released).

```yaml
setup_commands:
  - /fill -4 3 -4 20 12 4 glass 0 hollow
  - /fill 8 4 -3 19 10 3 water
  - /replaceitem entity @p slot.armor.chest elytra 1 0
  - /effect @p minecraft:levitation 2 20 true
  - /tp @p 0.5 8.0 0.5 -90 10
input:
  segments:
    - {seconds: 2.2, keys: [SPACE], buttons: []}
    - {seconds: 6.0, keys: [w], buttons: []}
duration_ticks: 180
```

**Assert:** both. Check elytra flag, hitbox/eye height, velocity transition at water entry, no swim pose, and liquid/elytra rendering. A second fixture can replace water with a shallow lava pool.

### 5. Slime-block bounce, sneaking suppression, and horizontal boost

**Behavior.** A non-sneaking living entity landing on slime takes zero fall damage and has a negative Y velocity negated exactly. Nonliving entities receive `-motionY * 0.8`. Sneaking suppresses the bounce and restores normal landing damage. While walking on slime with `abs(motionY) < 0.1` and not sneaking, X/Z velocity is multiplied by `0.4 + abs(motionY) * 0.2`. Slime slipperiness is `0.8`.

**1.11.2 caveat.** These are pre-honey-block rules. The often-stated âbounce to half the fall heightâ is an emergent result of gravity and drag, not the collision formula.

**Sources.** [slime-block bounce discussion](https://minecraft.wiki/w/Talk:Slime_Block), pinned 1.11.2 `BlockSlime#onFallen`, `onLanded`, and `onEntityWalk`.

```yaml
setup_commands:
  - /fill -4 3 -4 4 3 4 slime
  - /tp @p 0.5 20.0 0.5 0 0
input: {segments: [{seconds: 5.0, keys: [], buttons: []}]}
duration_ticks: 110
```

Run a paired tape with `keys: [SHIFT]` over the landing interval.

**Assert:** both. Compare pre/post-contact Y bit pattern, health/fall distance, rebound apex, sneak negative control, and slime pixels.

### 6. Cobweb asymmetric slowdown and fall-distance cancellation

**Behavior.** A web marks the entity `isInWeb`. On the next movement resolution, attempted X/Z displacement is multiplied by `0.25`, Y by `0.05000000074505806`, then all three stored motion components are zeroed. Entering the web also resets fall distance, allowing a bottom web to cancel a large fall.

**1.11.2 caveat.** This is the old `minecraft:web` block, not later cobweb behavior inferred from modern crawl/swim poses. Traversal time depends on input and contact geometry; assert the per-tick transform, not a folklore â13 seconds per blockâ number.

**Sources.** [cobweb mechanics overview](https://minecraft.fandom.com/wiki/Cobweb), pinned 1.11.2 `Entity#move` and `Entity#setInWeb`.

```yaml
setup_commands:
  - /fill -1 4 -1 1 6 1 web
  - /tp @p 0.5 12.0 0.5 0 0
input: {segments: [{seconds: 9.0, keys: [w], buttons: []}]}
duration_ticks: 190
```

**Assert:** both. Check first-contact fall-distance reset, displacement multipliers, zeroed stored velocity, no fall damage, and layered translucent pixels.

### 7. Sneak ledge clamp uses 0.05-block search increments

**Behavior.** When a grounded player is sneaking, attempted X and Z moves are repeatedly reduced toward zero in `0.05` increments until an AABB shifted down by one step finds support. Java applies separate X, Z, then diagonal loops. This produces distinctive corner behavior and tiny position plateaus rather than a single continuous clamp.

**1.11.2 caveat.** Piston translocation is not the same mechanism and was removed in 16w40a before 1.11 shipped. Magma has already fixed the basic sneak clamp, so target diagonal and fractional-speed cases.

**Sources.** [piston history noting 1.11 translocation removal](https://minecraft.wiki/w/Piston), pinned 1.11.2 `Entity#move`.

```yaml
setup_commands:
  - /fill -2 3 -2 0 3 0 stone
  - /tp @p -0.25 4.0 -0.25 -45 0
input: {segments: [{seconds: 4.0, keys: [w, SHIFT], buttons: []}]}
duration_ticks: 90
```

**Assert:** physics. Compare exact X/Z plateaus, on-ground state, corner ordering, and the paired non-sneak fall.

### 8. Soul sand collision height and compounded ice slowdown

**Behavior.** Soul sand has a `0.875`-block-tall collision box. Every collision multiplies X and Z velocity by `0.4`. Because it is shorter than a full cube, an entity visibly sinks by `0.125` block. In 1.11.2, ice slipperiness underneath still contributes, producing the historical ice-under-soul-sand slowdown.

**1.11.2 caveat.** The compounded slowdown was removed in the 1.15 development cycle, associated with MC-163952. Modern soul-sand bubble columns are irrelevant because bubble columns did not exist until 1.13.

**Sources.** [soul sand history and behavior](https://minecraft.fandom.com/wiki/Soul_Sand), [MC-163952](https://bugs.mojang.com/browse/MC-163952), pinned 1.11.2 `BlockSoulSand`.

```yaml
setup_commands:
  - /fill -8 2 -1 8 2 1 ice
  - /fill -6 3 -1 6 3 1 soul_sand
  - /tp @p -7.0 3.0 0.5 -90 0
input: {segments: [{seconds: 8.0, keys: [w, Control_L], buttons: []}]}
duration_ticks: 170
```

**Assert:** both. Compare Y at rest, repeated X/Z `*0.4`, entry/exit speed, and the paired soul-sand-over-stone control.

### 9. Ladder and vine clamps, sneak hold, and wall-contact climb

**Behavior.** While on a ladder or vine, X/Z motion is clamped to `[-0.15, 0.15]`, downward Y is clamped to at least `-0.15`, and fall distance is zeroed. A sneaking player with negative Y is held at `0`. If horizontally colliding while on the climbable, Java sets Y velocity to `0.2`.

**1.11.2 caveat.** Vines need a supporting face and have thinner collision/contact geometry than ladders. Trapdoor-assisted climbing and later scaffolding behavior are not 1.11.2 rules.

**Sources.** pinned 1.11.2 `EntityLivingBase#travel` and `EntityLivingBase#isOnLadder`.

```yaml
setup_commands:
  - /fill 0 3 -1 0 14 1 stone
  - /fill -1 4 0 -1 13 0 ladder 5
  - /tp @p -1.5 12.0 0.5 180 0
input:
  segments:
    - {seconds: 2.0, keys: [], buttons: []}
    - {seconds: 2.0, keys: [SHIFT], buttons: []}
    - {seconds: 3.0, keys: [w], buttons: []}
duration_ticks: 150
```

**Assert:** both. Check the three velocity clamps, fall distance, hold behavior, `0.2` climb impulse, and ladder/vine contact pixels.

### 10. Ice friction, airborne drag, and sprint-jump carry

**Behavior.** Ground X/Z drag is `block.slipperiness * 0.91`. Normal blocks use `0.6 * 0.91 = 0.546`; ice uses `0.98 * 0.91 = 0.8918`; airborne drag is `0.91`. Ground acceleration is scaled by `0.16277136 / drag^3`. Sprinting applies a 30% movement-speed attribute modifier, and sprint-jumping adds the familiar heading impulse before drag, preserving ice momentum.

**1.11.2 caveat.** Do not import modern sprint toggling or crawling rules. Exact last-bit values come from Java float-to-double promotion, so decimal-shortened reimplementations can visibly drift over a 100-tick runway.

**Sources.** pinned 1.11.2 `EntityLivingBase#travel`, `EntityPlayer#setSprinting`, and `BlockIce`.

```yaml
setup_commands:
  - /fill -30 3 -2 30 3 2 ice
  - /tp @p -20.5 4.0 0.5 -90 0
input:
  segments:
    - {seconds: 2.0, keys: [w, Control_L], buttons: []}
    - {seconds: 0.2, keys: [w, Control_L, SPACE], buttons: []}
    - {seconds: 5.0, keys: [], buttons: []}
duration_ticks: 150
```

**Assert:** both. Compare per-tick speed, jump impulse, landing speed, long slide distance, and ice rendering.

### 11. Head-in-block suffocation and inside-block camera overlay

**Behavior.** If any of eight small samples around the eye are inside an opaque block, Java attempts 1.0 HP `IN_WALL` damage each tick. Normal hurt resistance gates effective damage. Separately, first-person rendering selects an opaque block around the camera and draws its inside-block overlay.

**1.11.2 caveat.** Magma records player physics as matching but lists the camera-inside-opaque-block overlay as an open visual divergence. The two tests must be kept together because identical health does not prove identical camera selection.

**Sources.** pinned 1.11.2 `EntityPlayer#isEntityInsideOpaqueBlock`, `EntityPlayer#onLivingUpdate`, and the repo's `magma/OPEN_DIVERGENCES.md`.

```yaml
setup_commands:
  - /fill -1 4 -1 1 6 1 stone
  - /tp @p 0.5 4.0 0.5 0 0
input: {segments: [{seconds: 6.0, keys: [], buttons: []}]}
duration_ticks: 130
```

**Assert:** both. Check eye samples, health cadence, hurt timer, camera block identity, inside-block overlay, and escape on breaking one face.

### 12. Fence/wall 1.5-block collision versus one-block selection, including projectile bug

**Behavior.** Fence and cobblestone-wall collision boxes reach Y+`1.5`, although their visible/selection geometry is roughly one block high. Players cannot jump over them. In 1.11.2, MC-114722 reports that projectiles use the block hitbox/selection geometry rather than the taller collision box, so a projectile can pass through the invisible upper half that stops entities.

**1.11.2 caveat.** The projectile mismatch explicitly affects 1.11.2. Wall post/arm geometry depends on neighbors, so freeze an isolated wall and a straight connected wall as separate fixtures.

**Sources.** [MC-114722](https://bugs-legacy.mojang.com/browse/MC-114722), [collision-shape terminology](https://app.unpkg.com/minecraft-data@3.100.0/files/minecraft-data/doc/blockCollisionShapes.md), pinned 1.11.2 `BlockFence` and `BlockWall`.

```yaml
setup_commands:
  - /fill 2 4 -2 2 4 2 fence
  - /summon armor_stand 4.5 4 0.5 {NoGravity:1b,Marker:0b}
  - /give @p bow 1
  - /give @p arrow 16
  - /tp @p 0.5 4.0 0.5 -90 -8
input: {segments: [{seconds: 1.5, keys: [], buttons: [3]}, {seconds: 2.0, keys: [], buttons: []}]}
duration_ticks: 90
```

**Assert:** both. First attempt a jump against the fence; then fire through Y 1.0 to 1.5 above its base and compare projectile collision and pixels.

### 13. Water/lava flow cadence and deterministic block conversion

**Behavior.** In 1.11-era mechanics, water spreads up to seven horizontal blocks from a source and updates much faster than overworld lava. Lava advances one block per 30 game ticks in the Overworld/End and per 10 ticks in the Nether. Flowing lava contacting water from the side or above becomes cobblestone; a lava source contacted from the side/top becomes obsidian; lava flowing downward into water produces stone at the contact arrangement used by generators.

**1.11.2 caveat.** The 1.13 water rewrite changed flow handling, waterlogging, and many shapes. Java 1.11.2 has neither waterlogged block states nor bubble columns. Build fixtures from full cubes and known source levels rather than copying modern generator layouts blindly.

**Sources.** [fluid history and interaction rules](https://minecraft.wiki/w/Fluid), [obsidian formation](https://minecraft.wiki/w/Obsidian), [cobblestone generator mechanics](https://minecraft.wiki/w/Tutorial:Cobblestone_farming), [old lava cadence](https://minecraft.wiki/w/Minecraft_Wiki:Sandbox/Lava).

```yaml
setup_qrl:
  - cmd: runcmds
    action:
      commands: ["/fill -6 3 -3 6 3 3 stone", "/setblock -2 5 0 water", "/setblock 2 5 0 lava", "/tp @p 0.5 7 7.5 180 35"]
    settle_ticks: 200
input: {segments: [{seconds: 5.0, keys: [], buttons: []}]}
duration_ticks: 120
```

**Assert:** both. Compare block state/level at every update, conversion product and tick, flow top/side pixels, and emitted sound if captured. Use three tiny fixtures for side, source, and downward contacts.

### 14. Fall-damage formula and cancellation by one-block water, web, or ladder

**Behavior.** Living fall damage is `ceil((fallDistance - 3 - JumpBoostLevel) * multiplier)`, where active Jump Boost subtracts amplifier+1. Feather Falling contributes `3 * level` protection points, so level IV contributes 12, nominally 48% reduction before the shared protection cap. First entry into water resets fall distance even if the water is only one block deep; web and climbable contact also reset it before landing.

**1.11.2 caveat.** Fall distance accumulates downward displacement, not simply velocity or starting height. Water must intersect the player before solid-ground collision. Later waterlogging must not make partial blocks count as water here.

**Sources.** [Feather Falling protection summary](https://minecraft.wiki/w/Tutorial:Player_versus_Player), pinned 1.11.2 `EntityLivingBase#fall`, `Entity#handleWaterMovement`, and `EnchantmentProtection`.

```yaml
setup_commands:
  - /fill -1 3 -1 1 3 1 stone
  - /setblock 0 4 0 water
  - /tp @p 0.5 30.0 0.5 0 0
input: {segments: [{seconds: 4.0, keys: [], buttons: []}]}
duration_ticks: 100
```

**Assert:** both. Run solid, one-water, web, ladder-brush, and Feather Falling IV pairs. Check fall-distance reset tick, integer-ceiling damage, health, splash, and landing camera bob.

### 15. Depth Strider interpolation rather than a flat speed multiplier

**Behavior.** Depth Strider is capped at level 3 and halved when the player is not on the ground. At effective fraction `L/3`, horizontal water drag interpolates from `0.8` toward `0.54600006`, while acceleration interpolates from `0.02` toward the player's normal AI move speed. Y still follows water damping/gravity.

**1.11.2 caveat.** This is old walk/swim water movement, not the 1.13 swim-sprint model. Depth Strider and Frost Walker are mutually exclusive in normal enchanting, but commands can make impossible stacks, so do not use stacked boots as the oracle.

**Sources.** pinned 1.11.2 `EntityLivingBase#travel` and `EnchantmentHelper#getDepthStriderModifier`.

```yaml
setup_commands:
  - /fill -2 3 -2 20 7 2 glass 0 hollow
  - /fill -1 4 -1 19 6 1 water
  - /replaceitem entity @p slot.armor.feet diamond_boots 1 0 {ench:[{id:8s,lvl:3s}]}
  - /tp @p 0.5 4.0 0.5 -90 0
input: {segments: [{seconds: 6.0, keys: [w], buttons: []}]}
duration_ticks: 130
```

**Assert:** physics. Compare L0/L1/L3, grounded and mid-water starts, exact X/Z drag, acceleration, Y, and distance.

### 16. Boat status physics, controls, and 60-tick underwater ejection

**Behavior.** A 1.11.2 boat is `1.375 x 0.5625`. In water its horizontal momentum is `0.9`; under flowing water gravity is `-0.0007`; under source water vertical buoyancy is `+0.01` with momentum `0.45`; ordinary in-water buoyancy adds `(waterLevel - minY)/height * 0.06153846016296973` to Y then multiplies Y by `0.75`. Left/right changes yaw rate by 1 per tick, forward acceleration is `0.04`, backward is `-0.005`, and turning without thrust adds `0.005`. After 60 ticks in an underwater status it ejects passengers.

**1.11.2 caveat.** Boat physics were heavily rewritten around 1.9 and changed again later. Use this source-specific state machine, not current wiki speed tables. The entity ID became lower-case `boat` in 1.11.

**Sources.** [boat entity-ID history](https://minecraft.fandom.com/wiki/Boat), pinned 1.11.2 `EntityBoat#updateMotion`, `controlBoat`, and `onUpdate`.

```yaml
setup_commands:
  - /fill -4 3 -5 30 8 5 glass 0 hollow
  - /fill -3 4 -4 29 7 4 water
  - /summon boat 0.5 7.1 0.5
  - /tp @p 0.5 7.5 -1.0 0 20
input: {segments: [{seconds: 0.3, keys: [], buttons: [3]}, {seconds: 6.0, keys: [w, a], buttons: []}]}
duration_ticks: 150
```

**Assert:** both. Check boat status transitions, motion, yaw rate, passenger relation and ejection tick, camera, wake, and hull/water intersection.

### 17. Boat placement permits slight entity intersection

**Behavior.** The 1.11 boat-item placement path checks candidate entity collisions using an AABB shrunk by `0.1` on every side. MC-101334 demonstrates that this can place a boat slightly intersecting another entity or collision surface when an unshrunk box would reject it.

**1.11.2 caveat.** The issue dates from the 2016 boat implementation and the source pattern is present in the 1.11 line. Test placement, not `/summon`, because summoning bypasses the faulty item check.

**Sources.** [MC-101334](https://bugs-legacy.mojang.com/browse/MC-101334), pinned 1.11.2 `ItemBoat#onItemRightClick`.

```yaml
setup_commands:
  - /fill -2 3 -2 4 3 2 stone
  - /fill 1 4 -1 3 4 1 water
  - /summon armor_stand 2.25 4.0 0.5 {NoGravity:1b}
  - /give @p boat 1
  - /tp @p 0.5 4.0 0.5 -90 20
input: {segments: [{seconds: 0.2, keys: [], buttons: [3]}, {seconds: 2.0, keys: [], buttons: []}]}
duration_ticks: 60
```

**Assert:** both. Check item consumption, spawned boat AABB overlap and pose. Sweep stand X in command-defined 0.05 increments to bracket the acceptance boundary.

### 18. TNT entity trajectory, fuse, and explosion center

**Behavior.** Primed TNT has a default 80-tick fuse and size `0.98`. Its ordinary spawn constructor adds random horizontal launch velocity and `+0.2` Y, so deterministic tests must summon it with explicit `Motion`. Each tick applies `motionY -= 0.04`, moves, multiplies all axes by `0.98`, and on ground multiplies X/Z by `0.7` and Y by `-0.5`. It explodes with strength 4 at X/Z and `posY + height/16`.

**1.11.2 caveat.** Explosion ray sampling and drop decisions consume RNG. The expanding damage/knockback envelope is deterministic only when the world, entity list/order, and random state are fixed; trajectory and fuse are safer bit-exact assertions than every destroyed block.

**Sources.** pinned 1.11.2 `EntityTNTPrimed#onUpdate` and `World#createExplosion`.

```yaml
setup_commands:
  - /fill -8 3 -8 8 3 8 stone
  - /summon tnt 0.5 12.0 0.5 {Fuse:80s,Motion:[0.12d,0.0d,0.04d]}
  - /tp @p 5.5 4.0 0.5 90 0
input: {segments: [{seconds: 6.0, keys: [], buttons: []}]}
duration_ticks: 125
```

**Assert:** both. Check position/velocity/fuse every tick, first ground bounce, explosion tick and center, player damage/knockback, flash, and terrain pixels. Do not assert dropped-item identity.

### 19. Falling sand/gravel/anvil trajectory, landing, and damage

**Behavior.** Falling blocks are size `0.98`; each tick they apply Y `-0.04`, then multiply all axes by `0.98`. On ground X/Z are multiplied by `0.7` and Y by `-0.5` before placement. They time out as items after 100 ticks when outside Y 1..256, or after 600 ticks anywhere. A hurt-enabled falling block computes `i = ceil(fallDistance - 1)` and damage `min(floor(i * FallHurtAmount), FallHurtMax)`, with defaults 2 and 40 for anvils.

**1.11.2 caveat.** Anvil degradation is random with chance `0.05 + 0.05*i`, so assert trajectory and victim damage but not the resulting anvil wear state. Sand/gravel placement versus item drop depends on the landing block.

**Sources.** [falling-block lifetime overview](https://minecraft.wiki/w/Tutorial:Falling_blocks), pinned 1.11.2 `EntityFallingBlock#onUpdate` and `fall`.

```yaml
setup_commands:
  - /fill -2 3 -2 2 3 2 stone
  - /summon falling_block 0.5 20.0 0.5 {Block:"minecraft:anvil",Data:0b,Time:1,DropItem:0b,HurtEntities:1b,FallHurtAmount:2.0f,FallHurtMax:40}
  - /tp @p 0.5 4.0 0.5 0 0
input: {segments: [{seconds: 5.0, keys: [], buttons: []}]}
duration_ticks: 110
```

**Assert:** both. Check trajectory, fall distance, impact damage integer formula, placement tick, and entity-to-block visual transition; mask degradation.

### 20. XP-orb attraction is quadratic inside eight blocks

**Behavior.** An XP orb selects a nearby player within eight blocks. Let normalized distance be `d = distance/8`; attraction strength is `(1-d)^2 * 0.1`, applied along the normalized vector toward the player. Orb gravity is `0.03`; ordinary drag is `0.98`, or `block.slipperiness * 0.98` on ground; a ground bounce multiplies Y by `-0.9`. Orbs expire at age 6000.

**1.11.2 caveat.** Natural orb spawn velocity and merge/order effects are random or entity-order-sensitive. Summon exactly one orb with explicit position, value, and zero motion. Magma marks XP attraction as insufficiently pixel-verified.

**Sources.** pinned 1.11.2 `EntityXPOrb#onUpdate`; repo `magma/VERIFY.md`.

```yaml
setup_commands:
  - /fill -12 3 -2 12 3 2 stone
  - /summon xp_orb 7.5 4.25 0.5 {Value:7s,Age:0s,Motion:[0.0d,0.0d,0.0d]}
  - /tp @p 0.5 4.0 0.5 -90 0
input: {segments: [{seconds: 5.0, keys: [], buttons: []}]}
duration_ticks: 110
```

**Assert:** both. Check the no-attraction boundary just outside radius 8, quadratic acceleration within it, ground bounce, pickup tick/value, glow/bob pixels, and XP bar.

### 21. Elytra equation and the upward-motion activation bug

**Behavior.** Elytra gravity/lift starts with `motionY += -0.08 + cos(pitch)^2 * 0.06`. Falling converts 10% of downward speed into look-direction horizontal speed. Looking upward converts horizontal speed using `-sin(pitch) * 0.04` with a `3.2` vertical factor. Horizontal motion blends 10% toward the look vector, then drag is X/Z `0.9900000095367432` and Y `0.9800000190734863`. Wall damage is `max(10 * (speedBefore - speedAfter) - 3, 0)`. The flying player is `0.6 x 0.6` with eye height `0.4`.

**1.11.2 caveat.** MC-111444 is a shipped bug: elytra cannot be opened while the player is still moving upward. It was fixed in 19w42a. Opening requires airborne descending motion, jump held, usable equipped elytra, and no existing flight/creative state.

**Sources.** [MC-111444](https://bugs-legacy.mojang.com/browse/MC-111444), [historical elytra equation transcription](https://gist.github.com/samsartor/a7ec457aca23a7f3f120), [elytra mechanics overview](https://minecraft.fandom.com/wiki/Elytra), pinned 1.11.2 `EntityLivingBase#travel` and `EntityPlayerSP#onLivingUpdate`.

```yaml
setup_commands:
  - /fill -2 3 -3 40 25 3 glass 0 hollow
  - /replaceitem entity @p slot.armor.chest elytra 1 0
  - /effect @p levitation 1 6 true
  - /tp @p 0.5 12.0 0.5 -90 0
input: {segments: [{seconds: 1.2, keys: [SPACE], buttons: []}, {seconds: 6.0, keys: [SPACE, w], buttons: [], look: [0, -12]}]}
duration_ticks: 160
```

**Assert:** both. Assert activation remains false on rising ticks, first descending activation tick, exact equation afterward, size/eye change, wall damage, and pitch-dependent model/camera.

### 22. Shield delay, 180-degree blocking, and armor durability bug

**Behavior.** A shield becomes active only after five use ticks. In the 1.11 combat rules it blocks 100% of blockable damage, knockback, and applicable secondary effects from the front 180 degrees. For a blocked hit of at least 3 damage, shield durability loss is `ceil(incomingDamage)`. Axes can disable it. MC-98796 causes worn armor to lose durability even when the shield successfully blocks all health damage.

**1.11.2 caveat.** MC-98796 affects 1.11.2 and was fixed in 18w33a. The separate invisible axe-blocking bug MC-99688 was fixed in 1.11.2 itself, so it is a negative control, not a shipped 1.11.2 behavior.

**Sources.** [blocking history and five-tick delay](https://minecraft.wiki/w/Blocking), [MC-98796](https://bugs-legacy.mojang.com/browse/MC-98796), [MC-99688](https://bugs-legacy.mojang.com/browse/MC-99688).

```yaml
setup_commands:
  - /replaceitem entity @p slot.weapon.offhand shield 1 0
  - /replaceitem entity @p slot.armor.chest iron_chestplate 1 0
  - /summon skeleton 0.5 4.0 7.5 {NoAI:0b,HandItems:[{id:"minecraft:bow",Count:1b},{}]}
  - /tp @p 0.5 4.0 0.5 180 0
input: {segments: [{seconds: 5.0, keys: [], buttons: [3]}]}
duration_ticks: 120
```

**Assert:** both. Check pre-five-tick exposure, front versus rear health, knockback, shield and armor durability, use pose, blocked-hit pixels/sound. Use a command-spawned arrow if mob aim timing varies.

### 23. Hunger exhaustion thresholds and two healing loops

**Behavior.** Exhaustion is processed only when it becomes strictly greater than 4.0, subtracting 4.0 once per tick. Saturation loses 1 first; otherwise food loses 1. Sprint-jumping adds 0.2 exhaustion, a normal jump 0.05, attack 0.1, sprint movement 0.1 per metre, and swimming/diving 0.01 per metre; walking and sneaking add none. At full food with saturation, natural regeneration runs every 10 ticks, heals `min(saturation,6)/6`, and adds the same amount of exhaustion. At food at least 18 without saturation, it heals 1 HP every 80 ticks and adds 6 exhaustion. Normal-difficulty starvation attempts 1 HP every 80 ticks but stops at 1 HP.

**1.11.2 caveat.** These are the 1.11.2 fast-regeneration rules. Modern wiki summaries often describe later balancing and will give the wrong rates.

**Sources.** pinned 1.11.2 `FoodStats#onUpdate`, `EntityPlayer#jump`, `attackTargetEntityWithCurrentItem`, and `addMovementStat`.

```yaml
setup_commands:
  - /difficulty normal
  - /effect @p instant_damage 1 0 true
  - /fill -30 3 -2 30 3 2 stone
  - /tp @p -20.5 4.0 0.5 -90 0
input: {segments: [{seconds: 8.0, keys: [w, Control_L, SPACE], buttons: []}, {seconds: 8.0, keys: [], buttons: []}]}
duration_ticks: 340
```

**Assert:** both. Check exhaustion crossing, saturation/food decrement order, heal amount/tick, health/food HUD, and sprint-jump distance. Use setup NBT through QRL if the bridge exposes exact food/saturation initialization.

### 24. Totem of Undying 1.11.2 effects and exclusions

**Behavior.** The totem was added in Java 1.11, not 1.11.1. If held in either hand when otherwise-fatal damage arrives, it consumes one, sets health to 1.0, clears all active potion effects, then applies Regeneration II for 900 ticks and Absorption II for 100 ticks. It does not save a player from void or `/kill`-style creative-harming damage. Fire Resistance was not added to the totem until Java 1.16.2.

**1.11.2 caveat.** Some old prose pages say 40 seconds of regeneration; pinned 1.11.2 source says 900 ticks, or 45 seconds. Use the source value.

**Sources.** [official Totem of Undying history](https://www.minecraft.net/en-us/article/taking-inventory--totem-undying), [official 1.11.2 release context](https://www.minecraft.net/en-us/article/minecraft-1112-released), pinned 1.11.2 `EntityLivingBase#checkTotemDeathProtection`.

```yaml
setup_commands:
  - /replaceitem entity @p slot.weapon.offhand totem_of_undying 1 0
  - /effect @p poison 60 0 true
  - /summon tnt 0.5 4.0 0.5 {Fuse:40s,Motion:[0.0d,0.0d,0.0d]}
  - /tp @p 0.5 4.0 0.5 0 0
input: {segments: [{seconds: 8.0, keys: [], buttons: []}]}
duration_ticks: 180
```

**Assert:** both. Check consumption, 1.0 health, old-effect clearing, exact new effect duration/amplifier, absorption hearts, animation. Pair with a below-Y-64 fixture proving the totem is not consumed by void damage.

### 25. Sweeping attack gate, radius, damage, and knockback

**Behavior.** A sweep requires attack cooldown above 0.9, grounded, not sprinting, and low movement relative to base speed while using a sword. Secondary targets are selected in the primary target's AABB expanded by `(1.0, 0.25, 1.0)`, must be within distance squared 9, and receive `1.0 + sweepingRatio * primaryBaseDamage`; horizontal knockback is 0.4. Sweeping Edge was added in 1.11.1.

**1.11.2 caveat.** The primary attack and secondary sweep do not use the same damage formula. Do not infer the sweep from modern attack-indicator timing alone.

**Sources.** [official 1.11.2 notes](https://www.minecraft.net/en-us/article/minecraft-1112-released), [1.11.1 development history](https://minecraft.wiki/w/Java_Edition_1.11.1/Development_versions), pinned 1.11.2 `EntityPlayer#attackTargetEntityWithCurrentItem`.

```yaml
setup_commands:
  - /give @p diamond_sword 1 0 {ench:[{id:22s,lvl:3s}]}
  - /summon zombie 2.0 4.0 0.5 {NoAI:1b}
  - /summon zombie 2.0 4.0 1.35 {NoAI:1b}
  - /summon zombie 2.0 4.0 -0.35 {NoAI:1b}
  - /tp @p 0.5 4.0 0.5 -90 0
input: {segments: [{seconds: 1.2, keys: [], buttons: []}, {seconds: 0.1, keys: [], buttons: [1]}, {seconds: 2.0, keys: [], buttons: []}]}
duration_ticks: 75
```

**Assert:** both. Check cooldown, exact target set, primary versus secondary health, 0.4 knockback, arc particles/sound and sword swing.

### 26. Ender-pearl teleport, five damage, and fall-distance reset

**Behavior.** An ender pearl uses throwable drag `0.99` in air, `0.8` in water, and gravity `0.03`. On block impact it teleports the thrower, resets fall distance, and deals 5.0 HP using the fall-damage source. Feather Falling therefore reduces pearl damage. Entity impact deals 0 direct damage but can still provoke the struck entity. A successful player teleport has a 5% Endermite spawn roll when mob spawning is enabled.

**1.11.2 caveat.** Throw direction includes projectile inaccuracy and Endermite creation is random. Disable mob spawning, use a very near broad wall, and assert the impact/teleport contract rather than the whole free-flight arc.

**Sources.** pinned 1.11.2 `EntityEnderPearl#onImpact`, `EntityThrowable#onUpdate`, and `EnchantmentProtection`.

```yaml
setup_commands:
  - /gamerule doMobSpawning false
  - /fill 6 3 -3 6 10 3 stone
  - /give @p ender_pearl 1
  - /tp @p 0.5 8.0 0.5 -90 0
input: {segments: [{seconds: 0.1, keys: [], buttons: [3]}, {seconds: 4.0, keys: [], buttons: []}]}
duration_ticks: 100
```

**Assert:** both. Check impact tick, final position, fall-distance reset, exactly 5 attempted fall damage before enchantment reduction, health, inventory, particles, and camera discontinuity.

### 27. Minecart rail cap, diagonal speed, powered acceleration, and drag

**Behavior.** Rail-constrained X and Z are each capped at `0.4` block/tick, yielding 8 m/s cardinal speed but `0.4*sqrt(2)` blocks/tick, about 11.314 m/s, diagonally. Powered rails add `0.06` per tick along current motion. An unpowered powered rail stops a cart below `0.03`; otherwise it multiplies X/Z by `0.5`. Slope adjustment is `0.0078125`. Occupied-cart drag is `0.996999979019165`, empty-cart drag `0.9599999785423279`; a ridden cart also gets a pre-cap `0.75` multiplier.

**1.11.2 caveat.** Rails/minecarts are explicitly outside magma's current product contract, so this is a launch-scope decision rather than a mandatory gate. It remains an excellent faithful-port discriminator if any cart behavior is exposed.

**Sources.** [historical minecart speed summary](https://minecraft.wiki/w/Tutorial:Minecarts), pinned 1.11.2 `EntityMinecart#moveAlongTrack` and `applyDrag`.

```yaml
setup_commands:
  - /fill -2 3 -2 25 3 2 stone
  - /fill 0 4 0 20 4 0 rail
  - /summon minecart 0.5 4.1 0.5 {Motion:[0.4d,0.0d,0.0d]}
  - /tp @p -1.0 4.0 0.5 -90 0
input: {segments: [{seconds: 5.0, keys: [], buttons: []}]}
duration_ticks: 110
```

**Assert:** both if supported, otherwise mark out of scope. Use separate straight, diagonal, slope, powered, unpowered, occupied fixtures and compare cart motion, rail state, pose, and wheels.

### 28. Frost Walker source-only footprint and random melt boundary

**Behavior.** When the wearer is on ground, Frost Walker searches a radius `min(16, 2 + level)`. It can freeze only level-0 source water with air above, subject to normal placement checks. Each frosted-ice block schedules a melt update after a random 60 to 120 ticks.

**1.11.2 caveat.** The initial eligible footprint is deterministic; exact melt order is RNG-dependent. Magma blocks do not damage a Frost Walker wearer. Do not combine the enchantment with Depth Strider in the same oracle fixture.

**Sources.** pinned 1.11.2 `EnchantmentFrostWalker#freezeNearby`, `BlockFrostedIce#updateTick`, and `BlockMagma#onEntityWalk`.

```yaml
setup_commands:
  - /fill -8 3 -8 8 3 8 stone
  - /fill -7 4 -7 7 4 7 water
  - /replaceitem entity @p slot.armor.feet diamond_boots 1 0 {ench:[{id:9s,lvl:2s}]}
  - /tp @p 0.5 5.0 0.5 0 0
input: {segments: [{seconds: 2.5, keys: [w], buttons: []}]}
duration_ticks: 55
```

**Assert:** both for the first 55 ticks: exact source-block footprint, radius/height gate, player support, and frosted-ice pixels. Do not require melt order unless RNG state is also recorded.

### 29. Pre-1.13 partial blocks displace water instead of waterlogging

**Behavior.** Java 1.11.2 has no `waterlogged` property. Placing fences, slabs, stairs, signs, chests, and similar non-full blocks in water does not preserve a co-located water state as modern Java does. Resulting air pockets, flow recalculation, eye-water tests, and face rendering follow the old mutually exclusive block-state model.

**1.11.2 caveat.** Waterlogging and the broad water rewrite arrived in Java 1.13. Bubble columns and the modern swimming pose also arrived then. This is a negative capability that a port using modern block tables can easily get wrong.

**Sources.** [fluid history](https://minecraft.wiki/w/Fluid), pinned 1.11.2 block-state definitions.

```yaml
setup_qrl:
  - cmd: runcmds
    action:
      commands: ["/fill -3 3 -3 3 7 3 glass 0 hollow", "/fill -2 4 -2 2 6 2 water", "/setblock 0 4 0 wooden_slab", "/setblock 1 4 0 fence", "/tp @p 0.5 4.0 2.0 180 10"]
    settle_ticks: 80
input: {segments: [{seconds: 4.0, keys: [], buttons: []}]}
duration_ticks: 90
```

**Assert:** both. Check there is one block state, not a hidden water state; flow levels, air pockets, drowning eye test, culling, and surface pixels.

### 30. Slab/bed step-up and sneaking from half-block support

**Behavior.** Player step height is `0.6`, so a grounded player can step onto slabs and other 0.5-high obstacles without jumping if the collision sweep has headroom. Sneak-edge protection searches for support one full block below the shifted AABB; consequently a player can still walk off a lower slab when the drop from the current feet position is only 0.5, even though sneaking prevents a full-block ledge fall.

**1.11.2 caveat.** Beds have non-full collision height and orientation-dependent pixels, but there is no modern crawl-under-block mechanic. Use slab, bed, and snow-layer fixtures separately because their AABBs are not interchangeable.

**Sources.** [half-block movement note](https://minecraft.wiki/w/Half-blocks), pinned 1.11.2 `Entity#move`, `EntityPlayer` step height, `BlockSlab`, and `BlockBed`.

```yaml
setup_commands:
  - /fill -3 3 -1 3 3 1 stone
  - /fill 0 4 -1 2 4 1 stone_slab 0
  - /tp @p -2.5 4.0 0.5 -90 0
input: {segments: [{seconds: 3.0, keys: [w], buttons: []}, {seconds: 2.0, keys: [w, SHIFT], buttons: []}]}
duration_ticks: 110
```

**Assert:** both. Check automatic 0.5 step, no jump flag, collision choice, sneak behavior at the far slab edge, camera Y, and slab seam pixels.

### 31. Item-frame two-stage break, rotation, and comparator value

**Behavior.** A non-explosion attack on an item frame containing an item removes the displayed item first and leaves the frame; a later attack breaks the frame. Rotation is modulo 8. Comparator output is 0 when empty and `rotation % 8 + 1` when occupied. The displayed stack count is forced to 1.

**1.11.2 caveat.** Item frames are hanging entities with face-dependent position and hitbox, not blocks. `/summon` with explicit `Facing`, item, and rotation avoids placement ambiguity. Explosion behavior is a separate path.

**Sources.** pinned 1.11.2 `EntityItemFrame#setDisplayedItem`, `attackEntityFrom`, and `BlockComparator#getComparatorInputOverride` path.

```yaml
setup_commands:
  - /setblock 3 4 0 stone
  - /summon item_frame 2.96875 4.5 0.5 {Facing:4b,Item:{id:"minecraft:diamond",Count:64b},ItemRotation:7b}
  - /tp @p 0.5 4.0 0.5 -90 0
input: {segments: [{seconds: 0.1, keys: [], buttons: [1]}, {seconds: 1.0, keys: [], buttons: []}, {seconds: 0.1, keys: [], buttons: [1]}]}
duration_ticks: 70
```

**Assert:** both. Check forced count 1, first-hit item drop/frame survival, second-hit destruction, hitbox, rotation pixels, and comparator strength in an alternate redstone fixture.

### 32. Armor-stand size, Marker zero hitbox, and NoGravity

**Behavior.** A normal armor stand is `0.5 x 1.975`; `Small:1b` halves it to `0.25 x 0.9875`. `Marker:1b` sets size to zero and prevents normal collision/interaction. `NoGravity:1b` skips travel gravity. Pose limbs use explicit degree triples in NBT and are deterministic.

**1.11.2 caveat.** Marker stands can still render unless `Invisible` is set, despite having no collision volume. Modern equipment-slot locking and pose UI descriptions should not replace the 1.11.2 entity/NBT path.

**Sources.** pinned 1.11.2 `EntityArmorStand#setSize`, `setMarker`, `onUpdate`, and NBT methods.

```yaml
setup_commands:
  - /summon armor_stand 2.0 7.0 -1.0 {NoGravity:1b,Small:0b,Pose:{Head:[20f,0f,0f]}}
  - /summon armor_stand 2.0 7.0 0.5 {NoGravity:1b,Small:1b}
  - /summon armor_stand 2.0 7.0 2.0 {NoGravity:1b,Marker:1b}
  - /tp @p 0.5 4.0 0.5 -90 -20
input: {segments: [{seconds: 3.0, keys: [w], buttons: [1]}]}
duration_ticks: 80
```

**Assert:** both. Compare AABBs, gravity, collision/attack outcome, pose and scale pixels. The marker should not block the player or accept the ordinary hit.

### 33. Painting motive dimensions and support validation

**Behavior.** Painting motives have fixed pixel dimensions from 16x16 through 64x64. The hanging entity converts them to block-spanning AABBs with a thin face offset and periodically validates that every covered cell has support; loss of support breaks it. Ordinary item placement randomly chooses among valid motives, but an explicit `Motive` NBT removes that RNG.

**1.11.2 caveat.** Do not test random placement choice. In 1.11.2 motive identifiers and facing use the pre-flattening hanging-entity conventions even though entity registry names were normalized.

**Sources.** pinned 1.11.2 `EntityPainting`, `EntityHanging#updateBoundingBox`, and `onValidSurface`.

```yaml
setup_commands:
  - /fill 3 3 -3 3 8 3 stone
  - /summon painting 2.96875 5.0 0.5 {Facing:4b,Motive:"DonkeyKong"}
  - /tp @p 0.5 4.0 0.5 -90 -10
input: {segments: [{seconds: 4.0, keys: [], buttons: []}]}
duration_ticks: 90
```

**Assert:** both. Check motive, exact AABB/face offset, covered support cells, full image pixels, and deterministic break after a player removes one required support block.

### 34. Egg/snowball zero damage still causes knockback; snowballs hurt blazes

**Behavior.** Eggs and snowballs normally deal zero damage but still pass through the entity-hit damage path and can knock mobs back. Snowballs deal 3 damage to blazes. Since 15w49a, ordinary horizontal knockback does not add the old vertical lift when the target is airborne, so a 1.11.2 port must not import older airborne knockback.

**1.11.2 caveat.** Player-thrown projectile inaccuracy consumes RNG. Put a NoAI target very close and make its hitbox broad. Assert impact response rather than an exact long arc.

**Sources.** [knockback history and projectile cases](https://minecraft.wiki/w/Knockback_(mechanic)), pinned 1.11.2 `EntitySnowball#onImpact`, `EntityEgg#onImpact`, and `EntityLivingBase#knockBack`.

```yaml
setup_commands:
  - /summon zombie 2.5 4.0 0.5 {NoAI:1b}
  - /summon blaze 2.5 4.0 2.0 {NoAI:1b}
  - /give @p snowball 16
  - /tp @p 0.5 4.0 0.5 -90 0
input: {segments: [{seconds: 0.1, keys: [], buttons: [3]}, {seconds: 2.0, keys: [], buttons: []}]}
duration_ticks: 60
```

**Assert:** both. Check zombie health unchanged but velocity changed, blaze health minus 3, projectile death tick, impact particles, and no obsolete airborne Y boost.

### 35. Void damage cadence and totem/creative bypass

**Behavior.** Below Y `-64`, living entities call the out-of-world kill path, attempting 4.0 HP `OUT_OF_WORLD` damage each tick. That source can harm creative players and is excluded from Totem of Undying protection. Collision, armor, Resistance, and ordinary environmental immunity do not make it a normal fall.

**1.11.2 caveat.** Effective health cadence must be observed with the normal hurt timer rather than inferred as 4 HP removed every tick. The important shipped contract is the Y threshold, damage-source identity, and totem bypass.

**Sources.** [official totem void exclusion](https://www.minecraft.net/en-us/article/taking-inventory--totem-undying), pinned 1.11.2 `Entity#onEntityUpdate`, `EntityLivingBase#kill`, and `checkTotemDeathProtection`.

```yaml
setup_commands:
  - /replaceitem entity @p slot.weapon.offhand totem_of_undying 1 0
  - /tp @p 0 -65 0 0 0
input: {segments: [{seconds: 8.0, keys: [], buttons: []}]}
duration_ticks: 170
```

**Assert:** both. Check threshold crossing, health/hurt timer, source flags, unconsumed totem, death screen/void pixels, and creative-mode paired behavior.

### 36. Mounted entity and passenger do not collide with each other

**Behavior.** MC-110748 records that a vehicle and its direct passenger do not push or collide with each other in 1.11.2, including geometrically absurd combinations such as a cow riding a chicken. Their AABBs may overlap while passenger positioning follows the mount's update.

**1.11.2 caveat.** This affects 1.11.2 and dates to the post-14w27a passenger system. It is lower-ranked because most involved mob/vehicle stacks are outside magma's central product contract, but it is deterministic and visually obvious.

**Sources.** [MC-110748](https://bugs-legacy.mojang.com/browse/MC-110748), pinned 1.11.2 `Entity#canBePushed`, `isPassenger`, and passenger update methods.

```yaml
setup_commands:
  - /summon chicken 2.0 4.0 0.5 {NoAI:1b,Passengers:[{id:"cow",NoAI:1b}]}
  - /summon cow 5.0 4.0 0.5 {NoAI:1b,Passengers:[{id:"chicken",NoAI:1b}]}
  - /tp @p 0.5 4.0 0.5 -90 0
input: {segments: [{seconds: 5.0, keys: [], buttons: []}]}
duration_ticks: 110
```

**Assert:** both. Check mount/passenger AABBs, absence of mutual push, passenger offsets/order, and model overlap pixels.

## Useful visual-only bug probes below the main cut

These are real 1.11.2-era issues, but they rank below the 36 mechanics above because magma lacks the relevant particle/model subsystem or the canonical harness view hides them.

- **Sneaking feet clip into the ground:** [MC-48191](https://bugs-legacy.mojang.com/browse/MC-48191) affects 1.11.2. It is deterministic in third-person, but the standard first-person tape does not render the local player body.
- **Boat/lily-pad excessive particles:** [MC-96207](https://bugs-legacy.mojang.com/browse/MC-96207) affects 1.11.2 and was fixed in 19w38a. The event is scriptable, but particle count/order is RNG-sensitive and magma presently has no full particle system.
- **Underwater block-selection outline:** [MC-117513](https://bugs-legacy.mojang.com/browse/MC-117513) reports the outline becoming invisible underwater in 1.11.2. Use only after crosshair/selection rendering is a launch gate.
- **Banner pattern differs when applied to shield:** [MC-86135](https://bugs-legacy.mojang.com/browse/MC-86135) affects 1.11.2. It is a stable inventory/render fixture, but lower value than shield mechanics.
- **Boat can ride a minecart:** [MC-113871](https://bugs-legacy.mojang.com/browse/MC-113871) affects 1.11.2 and is visually excellent, but both rail support and nested vehicles are outside the current magma contract.

## Do not bother in this harness

- **Thorns proc and damage:** proc chance and returned damage are RNG-driven (`15% * level`, then randomized damage). It can be seed-replayed but is a poor bit-exact launch gate because unrelated RNG calls perturb it.
- **Fishing loot, bite timing, and bobber splashes:** weather, open-water conditions, lure timing, loot tables, and multiple RNG streams make a <=2000-tick tape brittle. A forced bobber collision would test only generic projectile code.
- **Lightning fire placement and mob conversions:** `/summon lightning_bolt` fixes the strike point, but secondary fire attempts, flash lifetime, and nearby conversion availability consume RNG; weather-natural strikes are worse.
- **Squid and guardian AI:** their wander/beam target choices are RNG- and entity-order-sensitive. Recorded entity playback can validate pixels, but not magma's independent mechanics, so this does not expose a faithful simulation gap cleanly.
- **Natural TNT ignition and exact block-drop set:** the constructor impulse and explosion ray/drop paths consume RNG. Use candidate 18's explicit `Motion` and assert trajectory, player knockback, and coarse terrain result instead.
- **Anvil degradation state:** impact damage is deterministic, degradation is not. Mask the post-impact anvil damage level.
- **Frosted-ice melt order:** scheduled delay is random 60 to 120 ticks. Assert the initial freeze footprint within 55 ticks instead.
- **Endermite from an ender pearl:** fixed 5% roll. Disable `doMobSpawning` and test teleport/damage.
- **Random painting selection:** the game shuffles among valid motives. Set `Motive` explicitly and test geometry/support.
- **Natural armor-stand or item-frame drops under explosions:** explosion and drop order add RNG and entity-order dependence. Use direct attacks.
- **Save/reload fall-damage cancellation:** [MC-212](https://bugs-legacy.mojang.com/browse/MC-212) depends on persistence/reload, while the scenario harness creates a fresh world and has no mid-tape save/reload action.
- **Elytra unloaded-chunk desync:** [MC-90026](https://bugs-legacy.mojang.com/browse/MC-90026) depends on chunk I/O and timing, so it is not a deterministic short local tape.
- **Piston/slime block duplication:** [MC-112026](https://bugs-legacy.mojang.com/browse/MC-112026) is redstone-timing and save-state sensitive, and pistons are outside the current product surface. It is also a duplication exploit rather than a visible player-physics invariant.
- **Old piston translocation/corner clipping:** it was removed in 16w40a before Java 1.11. Treat âpiston translocation works in 1.11.2â as a false premise and use it only as a negative control. [Piston history](https://minecraft.wiki/w/Piston).
- **Shield invisible-blocking axe bug:** [MC-99688](https://bugs-legacy.mojang.com/browse/MC-99688) was fixed in 1.11.2, so reproducing it would be incorrect. The armor-durability bug MC-98796 is the one that ships.
- **Modern waterlogging, bubble columns, crawling, and swim sprint:** none exists in 1.11.2. They arrived in or after the 1.13 aquatic rewrite and should be negative capability tests, not expected mechanics. [Fluid history](https://minecraft.wiki/w/Fluid).

## Recommended first ten tapes

If time permits only ten additions, implement ranks 1, 2, 3, 4, 5, 6, 8, 11, 12, and 13. Together they cover the two known magma visual gaps, source-exact movement constants, two shipped MC bugs, old fluid state/flow, and collision-versus-selection geometry. Add rank 22 next if inventory durability is available in the recorded assertion stream.
